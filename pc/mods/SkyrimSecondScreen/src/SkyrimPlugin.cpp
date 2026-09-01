#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>

#include "Bridge.h"
#include "CustomPlayerPreview.h"

namespace {

using UInt32 = uint32_t;
using PluginHandle = UInt32;

struct PluginInfo {
  enum { kInfoVersion = 1 };
  UInt32 infoVersion;
  const char* name;
  UInt32 version;
};

struct SKSEInterface {
  UInt32 skseVersion;
  UInt32 runtimeVersion;
  UInt32 editorVersion;
  UInt32 isEditor;
  void* (*QueryInterface)(UInt32 id);
  PluginHandle (*GetPluginHandle)();
  UInt32 (*GetReleaseIndex)();
  const PluginInfo* (*GetPluginInfo)(const char* name);
};

struct SKSEMessagingInterface {
  struct Message {
    const char* sender;
    UInt32 type;
    UInt32 dataLen;
    void* data;
  };
  using EventCallback = void(*)(Message* message);
  UInt32 interfaceVersion;
  bool (*RegisterListener)(PluginHandle listener, const char* sender, EventCallback handler);
  bool (*Dispatch)(PluginHandle sender, UInt32 messageType, void* data, UInt32 dataLen,
                   const char* receiver);
  void* (*GetEventDispatcher)(UInt32 dispatcherId);
};

struct SKSESerializationInterface;
using SerializationCallback = void(*)(SKSESerializationInterface*);

struct SKSESerializationInterface {
  UInt32 interfaceVersion;
  void (*SetUniqueID)(PluginHandle, UInt32);
  void (*SetRevertCallback)(PluginHandle, SerializationCallback);
  void (*SetSaveCallback)(PluginHandle, SerializationCallback);
  void (*SetLoadCallback)(PluginHandle, SerializationCallback);
  void (*SetFormDeleteCallback)(PluginHandle, void*);
  bool (*WriteRecord)(UInt32, UInt32, const void*, UInt32);
  bool (*OpenRecord)(UInt32, UInt32);
  bool (*WriteRecordData)(const void*, UInt32);
  bool (*GetNextRecordInfo)(UInt32*, UInt32*, UInt32*);
  UInt32 (*ReadRecordData)(void*, UInt32);
  bool (*ResolveHandle)(uint64_t, uint64_t*);
  bool (*ResolveFormId)(UInt32, UInt32*);
};

constexpr UInt32 kRuntime1597 = 0x01050610;
constexpr UInt32 kMessagingInterface = 5;
constexpr UInt32 kSerializationInterface = 3;
constexpr UInt32 kMessagePreLoadGame = 2;
constexpr UInt32 kMessagePostLoadGame = 3;
constexpr UInt32 kMessageNewGame = 7;
constexpr UInt32 kMessageDataLoaded = 8;
constexpr uintptr_t kPlayerPtr = 0x02F26EF8;
constexpr uintptr_t kActorValueListPtr = 0x01EBE418;
constexpr uintptr_t kTlsIndexPtr = 0x03497408;
constexpr uintptr_t kPlayerActorValueVtable = 0x01664098;
constexpr uintptr_t kActorValueGetCurrent = 0x00620D60;
constexpr uintptr_t kActorValueGetMaximum = 0x00620E60;
constexpr uintptr_t kMagicFavoritesPtr = 0x02F26730;
constexpr uintptr_t kEquipManagerPtr = 0x02EC4838;
constexpr uintptr_t kDataHandlerPtr = 0x01EBE428;
constexpr uintptr_t kDynamicCast = 0x0134BDB0;
constexpr uintptr_t kRttiBSExtraData = 0x01E12060;
constexpr uintptr_t kRttiExtraContainerChanges = 0x01E13C70;
constexpr uintptr_t kRttiTESForm = 0x01E10E30;
constexpr uintptr_t kRttiTESBoundObject = 0x01E10EB8;
constexpr uintptr_t kRttiTESContainer = 0x01E15488;
constexpr uintptr_t kRttiTESFullName = 0x01E10F00;
// Address Library ID 229886, Skyrim SE 1.5.97. Matching the concrete
// ExtraContainerChanges vtable is safer than invoking __RTDynamicCast while
// the player's ExtraDataList is settling after a save load.
constexpr uintptr_t kExtraContainerChangesVtable = 0x0152F260;
constexpr uintptr_t kGetEntryValue = 0x001D66E0;
constexpr uintptr_t kGetFormWeight = 0x001A1730;
constexpr uintptr_t kGetBaseValue = 0x001A14E0;
constexpr uintptr_t kGetDamage = 0x00693050;
constexpr uintptr_t kGetArmor = 0x00692CD0;
constexpr uintptr_t kGetLevel = 0x005D62E0;
constexpr uintptr_t kLookupForm = 0x00194230;
// Address Library ID 12204, BSPointerHandleManagerInterface<TESObjectREFR>
// smart-pointer lookup for Skyrim SE 1.5.97.
constexpr uintptr_t kLookupReferenceByHandle = 0x001328A0;
// Skyrim SE 1.5.97 MapMenu's exact active-objective marker path. The active
// target lookup owns PlayerCharacter::questTargets locking internally; these
// helpers are only called from the post-original PlayerCharacter update hook.
constexpr uintptr_t kGetActiveQuestTargets = 0x006A3A70;
constexpr uintptr_t kResolveQuestTargetHandle = 0x00383C40;
constexpr uintptr_t kGetTeleportData = 0x002A71A0;
// Address Library IDs 24537 and 19354 for Skyrim SE 1.5.97. These are the
// same quest-alias lookup and reference display-name paths used by vanilla UI.
constexpr uintptr_t kCreateRefHandleByAliasId = 0x00378760;
constexpr uintptr_t kGetReferenceDisplayName = 0x002961F0;
constexpr uintptr_t kEquipItem = 0x00637A80;
// Address Library ID 37939, ActorEquipManager::EquipSpell for 1.5.97.
constexpr uintptr_t kEquipSpell = 0x00637BC0;
// Address Library ID 37941, ActorEquipManager::EquipShout for 1.5.97.
constexpr uintptr_t kEquipShout = 0x00637CA0;
constexpr uintptr_t kUnequipItem = 0x00638190;
// Address Library IDs 37947/37948. These are the exact native magic removal
// paths used by FavoritesMenu; clearing Actor::selectedSpells directly leaves
// the animation graph and equipped state out of sync.
constexpr uintptr_t kUnequipSpell = 0x006383B0;
constexpr uintptr_t kUnequipShout = 0x00638420;
// Address Library IDs 11213/14399/11045/66861 for Skyrim SE 1.5.97.
constexpr uintptr_t kCalculateMagicCost = 0x00101A30;
constexpr uintptr_t kGetDescription = 0x00190480;
constexpr uintptr_t kGetMemoryManager = 0x000FCFE0;
constexpr uintptr_t kMemoryManagerDeallocate = 0x00C02560;
// Address Library ID 261916 resolves to PlayerCharacter's primary vtable at
// SkyrimSE+0x16635E0 for the only supported runtime (1.5.97). Actor::Update is
// virtual slot 0xAD and points to SkyrimSE+0x69E580 in the exact executable.
// That routine reads Skyrim's TLS at entry, making its caller the safe place
// to sample engine-owned state without patching translated executable code.
constexpr uintptr_t kPlayerVtable = 0x016635E0;
constexpr size_t kPlayerUpdateSlot = 0xAD;
constexpr uintptr_t kPlayerUpdate = 0x0069E580;
// Read-only Actor predicates used to ensure the cloned third-person tree is
// sampled from Skyrim's fully evaluated neutral standing state.
constexpr uintptr_t kActorIsMoving = 0x00608B30;
constexpr uintptr_t kActorIsInMidair = 0x005D1620;
constexpr uintptr_t kActorIsInRagdollState = 0x005E33B0;
// Canonical Address Library 1.5.97 RVAs.  These are Skyrim's existing
// Inventory3DManager renderer and renderer-data global; no replacement scene,
// swap chain, or guessed signature is created.
constexpr uintptr_t kInventory3DManagerPtr = 0x02F27180;
constexpr uintptr_t kInventory3DInitialize = 0x00887600;
constexpr uintptr_t kInventory3DRender = 0x00887750;
constexpr uintptr_t kInventory3DShutdown = 0x00887810;
constexpr uintptr_t kInventory3DUpdateItem = 0x00887970;
// Inventory3DManager::UpdateMagic3D(TESForm*, uint32_t).  Unlike the
// UpdateItem3D wrapper above, this entry receives the owning SpellItem/Shout
// and lets Skyrim resolve its MDOB and initialize the effect controller and
// shader material state used by the vanilla Magic menu.
constexpr uintptr_t kInventory3DUpdateMagic = 0x008879A0;
// Inventory3DManager::Render calls this immediately before updating the
// selected model's controllers and dispatching UI3DSceneManager::Render.  The
// camera must be calculated after this transform has been applied; otherwise
// the NIF's load-time bound is framed while a differently scaled/translated
// model is submitted.
constexpr uintptr_t kInventory3DUpdateTransform = 0x00888A80;
// Boolean value read by Inventory3DManager::UpdateItem3D before it schedules
// the asynchronous NIF load (bShowInventory3D:Interface on Skyrim SE 1.5.97).
constexpr uintptr_t kShowInventory3DSetting = 0x01DF3EF0;
constexpr uintptr_t kRendererDataPtr = 0x03025F00;
constexpr uintptr_t kCurrentRenderWindowPtr = 0x03025F10;
// Canonical Skyrim SE 1.5.97 Address Library RVAs.  The character preview uses
// Skyrim's own cloned NiAVObject tree and UI3D scene renderer.  It renders into
// a private texture; no additional swap chain or second gameplay Actor exists.
constexpr uintptr_t kNiAVObjectClone = 0x00C52750;
constexpr uintptr_t kNiVisControllerVtable = 0x01779818;
// BSFlattenedBoneTree is the specialized "NPC Root [Root]" node in Skyrim's
// actor skeleton. Its 1.5.97 Clone implementation allocates the flattened bone
// records as a C++ array (the stored element pointer follows an 8-byte count
// cookie), while its ordinary destructor sends that stored element pointer to
// MemoryManager as if it were the allocation base. Native actors do not use
// this clone-only allocation shape. Retire cloned arrays through their emitted
// array-deleting destructor before the normal tree destructor sees them.
constexpr uintptr_t kBSFlattenedBoneTreeVtable = 0x01767A18;
constexpr uintptr_t kBSFlattenedBoneRecordArrayDelete = 0x00C6A790;
constexpr uintptr_t kBSCullingProcess1 = 0x00D50C80;
constexpr uintptr_t kUI3DSceneManagerPtr = 0x02F27188;
constexpr uintptr_t kUI3DAttachChild = 0x008D33B0;
constexpr uintptr_t kUI3DDetachChild = 0x008D3480;
constexpr uintptr_t kUI3DDetachChildScheme = 0x008D34A0;
constexpr uintptr_t kUI3DRender = 0x008D2390;
constexpr uintptr_t kUI3DInitializeScheme = 0x008D3550;
constexpr uintptr_t kUI3DShutdownScheme = 0x008D3610;
constexpr uintptr_t kUI3DSaveCamera = 0x008D3710;
constexpr uintptr_t kUI3DRestoreCamera = 0x008D37F0;
constexpr uintptr_t kUI3DSetCameraPosition = 0x008D38E0;
constexpr uintptr_t kUI3DLookAt = 0x008D3930;
constexpr uintptr_t kUI3DSetCameraFov = 0x008D39E0;
// Internal helper used by UI3DSceneManager when a menu changes schemes.  It
// removes the old scheme's lights and activates the requested scheme's lights;
// changing currentlightScheme alone leaves the renderer using stale lighting.
constexpr uintptr_t kUI3DSetCurrentLightScheme = 0x008D4540;
// Skyrim SE 1.5.97 HUDMenu primary vtable. IMenu::PostDisplay is virtual slot
// 6 and runs inside Skyrim's UI render phase every frame while the HUD is
// active. InventoryMenu calls Inventory3DManager::Render from this same phase;
// using HUDMenu gives the second-screen renderers that cadence without opening
// or pausing a menu.
constexpr uintptr_t kHudMenuVtable = 0x016B2868;
constexpr size_t kHudPostDisplaySlot = 6;
// Skyrim SE 1.5.97 InventoryMenu primary vtable.  The diagnostic hook around
// PostDisplay is a control group only: it observes Skyrim's normal working
// UI3D render without changing its camera, scene, target, or menu state.
constexpr uintptr_t kInventoryMenuVtable = 0x016B4600;
constexpr size_t kInventoryPostDisplaySlot = 6;
constexpr uintptr_t kRendererSingleton = 0x03028490;
// BSGraphics::RenderTargetManager. The generic viewport helper uses the active
// render target's +0/+4 dimensions, but UI3DSceneManager::Render additionally
// selects depth target zero and passes depthStencilTargetData[0].width/height
// (+0xC78/+0xC7C) directly to BSGraphics::SetViewport at
// SkyrimSE+0x8D2730. Both records must describe the private color/depth pair;
// changing only target zero leaves UI3D's accumulator at the 1280x720 gameplay
// viewport and projects real preview geometry mostly outside the texture.
constexpr uintptr_t kRenderTargetManager = 0x0302BB20;
constexpr uintptr_t kScreenWidth = 0x0302C8B4;
constexpr uintptr_t kScreenHeight = 0x0302C8B8;
constexpr size_t kRendererFramebufferOffset = 0xA58;
constexpr size_t kRendererFramebufferWidthOffset = 0x0;
constexpr size_t kRendererFramebufferHeightOffset = 0x4;
constexpr size_t kRendererDepthStencilWidthOffset = 0xC78;
constexpr size_t kRendererDepthStencilHeightOffset = 0xC7C;
// BSGraphics::ApplyRenderTargets at SkyrimSE+0xD6A330 resolves every ordinary
// active color-target ID from RendererData + 0xA58 + id * 0x30 and loads the
// RTV at record +0x10.  +0x26D0 is a separate cube/render-target-variant table;
// treating it as the ordinary color table made the private map transaction
// search unrelated pointers and abort before Main_RenderWorld could draw.
// Depth IDs are resolved from the same RendererData object at +0x1FC0.
constexpr size_t kRendererColorTargetDataOffset = 0xA58;
constexpr uint32_t kRendererColorTargetCount = 114;
// Renderer is a 0x10-byte wrapper around RendererData.  DepthStencilData
// therefore begins at Renderer+0x1FB8; its DSV arrays are selected by
// BSGraphics::ApplyRenderTargets at Renderer+0x1FC0 and +0x2000.
constexpr size_t kRendererDepthTargetDataOffset = 0x1FB8;
constexpr uint32_t kRendererDepthTargetCount = 12;
constexpr uint32_t kRendererDepthViewEntriesPerTarget = 16;
// Cubemap/variant RTVs are the one target family ApplyRenderTargets resolves
// through *(SkyrimSE+0x3025F00), rather than through the Renderer singleton.
constexpr size_t kRendererSpecialColorViewOffset = 0x26D0;
constexpr uint32_t kRendererSpecialColorViewCount = 6;
constexpr uint32_t kRendererColorPatchCapacity =
    kRendererColorTargetCount + kRendererSpecialColorViewCount +
    D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
constexpr size_t kRendererTargetMetadataStride = 0x1C;
constexpr size_t kRendererDepthMetadataStride = 0x10;
constexpr uintptr_t kRendererStateDirtyFlags = 0x03027EB0;
constexpr uintptr_t kRendererActiveRenderTarget = 0x03027EC8;
constexpr uintptr_t kRendererActiveDepthStencil = 0x03027EE8;
constexpr uintptr_t kRendererRenderTargetModes = 0x03027EF8;
// Canonical 1.5.97 UI singleton and MapMenu primary vtable.  The former
// implementation treated unrelated bytes near a renderer-global table as
// world-map state.  Those bytes never changed when MapMenu opened, so native
// capture was permanently gated off.  Read the real UI menu stack instead.
constexpr uintptr_t kUIPtr = 0x01EBEB20;          // Address Library ID 514178
// RE::UI 1.5.97 runtime fields. UI3DSceneManager::Render calls
// UI::GameIsPausedMenuDisabled (SkyrimSE+0xEBE310) and returns before issuing
// any draw work unless numDisablePauseMenu is non-zero. Native inventory,
// barter, magic, and stats menus satisfy this by carrying
// IMenu::kDisablePauseMenu while they are on UI::menuStack. Our second-screen
// render is deliberately not a gameplay menu, so it must reproduce that one
// scoped renderer precondition without opening or pausing a native menu.
constexpr size_t kUiNumDisablePauseMenuOffset = 0x168;
constexpr size_t kUiNumCustomRenderingOffset = 0x174;
// Main_RenderWorld 1.5.97 repeatedly loads this BSSceneGraph and its NiCamera
// at +0x128 (for example SkyrimSE+0x12E3EA4 and +0x12E3F44).  0x2F26EC0 is a
// different renderer-era global and must not be used as the map scene graph.
constexpr uintptr_t kWorldRootNodePtr = 0x032334B8;
// Canonical Main::WorldRootNode used by vanilla MapMenu/MapCamera.  This is
// deliberately separate from the renderer BSSceneGraph above.
constexpr uintptr_t kNativeMapWorldRootNodePtr = 0x02F26EC0;
// Main_RenderWorld publishes the chosen scene camera here at +0x12E3EB2.
// Restore it with the scene-graph pointer before returning from the isolated
// pass so no between-pass system can observe the private camera.
constexpr uintptr_t kCurrentWorldRenderCamera = 0x03495D48;
// BSSceneGraph::Render/BSShaderAccumulator publishes its active camera here at
// SkyrimSE+0x12C1649. UI3D uses that lower scene boundary too, so a private map
// draw must restore this slot even though it never mutates PlayerCamera or the
// live BSSceneGraph camera pointer.
constexpr uintptr_t kShaderSceneCamera = 0x031D0E68;
// Exact Skyrim SE 1.5.97 world-map camera/render contract. The old producer
// sent WorldRoot through UI3DSceneManager and crashed in shadow traversal.
// This producer instead inserts one requested map pass at Skyrim's own
// Main_RenderWorld callsite, then runs the original gameplay pass normally.
// World-map activation does not itself create/push MapMenu, pause the game,
// or capture input; those behaviours belong to IMenu and are never invoked.
constexpr uintptr_t kWorldRenderCallsite = 0x005B2091;
constexpr uintptr_t kMainRenderWorld = 0x012E3E70;
constexpr uintptr_t kMapCameraConstructor = 0x008DEAA0;
constexpr uintptr_t kMapCameraDestructor = 0x008DEE90;
constexpr uintptr_t kMapCameraSetWorldRoot = 0x008DEF30;
constexpr uintptr_t kMapCameraUpdate = 0x008DF1C0;
constexpr uintptr_t kMapCameraRestorePlayerVisibility = 0x008DF2C0;
constexpr uintptr_t kMapCameraResolvePosition = 0x008DF420;
constexpr uintptr_t kMapCameraWorldSetPosition = 0x008E08F0;
// TESCamera::SetState. MapCamera::SetWorldRoot initializes a World state and
// then installs its animated Transition state through this routine. An
// isolated, single-frame map capture has no real-time transition window, so it
// installs that already-initialized World state through the same native state
// ownership path before rendering.
constexpr uintptr_t kCameraSetState = 0x004F5C80;
// Narrow scene/LOD transaction called by vanilla world-map activation.  The
// broad ActivateWorldMapScene path also replaces shared gameplay renderer,
// weather, camera, and view globals; using it for every lower-screen frame is
// what made the top-screen camera flash. These two routines perform only the
// paired visibility/LOD preparation required by Main_RenderWorld(true).
constexpr uintptr_t kPrepareWorldMapScene = 0x008E8E10;
constexpr uintptr_t kRestoreWorldMapScene = 0x008E9140;
// Exact 1.5.97 NiCamera projection wrapper used by MapMenu::RefreshMarkers.
constexpr uintptr_t kWorldPointToScreen = 0x00C66530;
constexpr uintptr_t kGetCurrentWorldSpace = 0x008E8CA0;
constexpr uintptr_t kNullMapCameraCallbacksVtable = 0x016C21E8;
// First byte set by ActivateWorldMapScene and cleared by its paired teardown.
// Refuse nested activation when the player has opened Skyrim's own MapMenu.
constexpr uintptr_t kWorldMapSceneActive = 0x02F4F023;
constexpr int32_t kCharacterPreviewScheme = 5;
// UI3DSceneManager::INTERFACE_LIGHT_SCHEME::kInventory.  Scheme 6 is Stats
// and caused UpdateItem3D's asynchronous completion to attach elsewhere.
constexpr int32_t kItemPreviewScheme = 1;
constexpr unsigned kNativeCaptureMaxAttempts = 10;
// A retry includes an asynchronous Present/readback round trip. The extra
// bounded passes leave room for one discovery frame plus measured pan/fit
// corrections on slower translated frames.
constexpr uint64_t kNativeCaptureRetryWindowMs = 2400;
constexpr uint64_t kNativeCaptureRetrySpacingMs = 80;
// A lost or wedged translated-GPU copy must not freeze either preview state
// machine forever. Normal Thor readbacks complete in one or two Presents.
constexpr uint64_t kNativeReadbackTimeoutMs = 2000;
constexpr uint64_t kNativeReadbackAbortMs = 5000;
constexpr char kPluginBuildIdentity[] = __DATE__ " " __TIME__;
using PlayerUpdateFn = void(*)(void* player, float delta_seconds);

template <typename Fn>
Fn ResolveProxyExport(HMODULE module, const char* name) {
  static_assert(sizeof(Fn) == sizeof(FARPROC),
                "Windows function pointers must match FARPROC size");
  const FARPROC address = module ? GetProcAddress(module, name) : nullptr;
  Fn function = nullptr;
  std::memcpy(&function, &address, sizeof(function));
  return function;
}

uintptr_t g_base = 0;
SKSEMessagingInterface* g_messages = nullptr;
SKSESerializationInterface* g_serialization = nullptr;
dis::SubmitSnapshotFn g_submit = nullptr;
dis::SubmitItemPreviewFn g_submit_item_preview = nullptr;
dis::PollCommandFn g_poll = nullptr;
dis::HasPendingCommandFn g_has_pending_command = nullptr;
dis::ReportActionSuccessFn g_report_action_success = nullptr;
dis::EnableRendererFn g_enable_renderer = nullptr;
dis::GetCharacterPreviewSurfaceFn g_get_character_surface = nullptr;
dis::SetCharacterPreviewReadyFn g_set_character_ready = nullptr;
dis::SubmitCharacterPreviewFn g_submit_character_preview = nullptr;
dis::SubmitMapPreviewFn g_submit_map_preview = nullptr;
bool g_native_item_models_enabled = true;
// The legacy actor-clone/UI3D character renderer remains disabled below. This
// flag controls the replacement NIF/DDS renderer, which owns an independent
// skeleton, camera, lighting setup, and offscreen render target.
bool g_custom_character_preview_enabled = true;
bool g_native_character_preview_enabled = false;
bool g_native_map_enabled = true;
// Preserve the native map implementation for later work, but do not install
// its render hook or accept map requests while the page is hidden.
constexpr bool kNativeMapTemporarilyDisabled = true;
uint64_t g_custom_preview_manifest_signature = 0;
uint64_t g_custom_preview_manifest_revision = 0;
std::atomic<bool> g_running{false};
std::atomic<bool> g_data_loaded{false};
std::atomic<bool> g_game_ready{false};
std::atomic<bool> g_live_snapshot_ready{false};
std::atomic<bool> g_renderer_ready{false};
std::atomic<uint64_t> g_renderer_not_before{0};
constexpr uint64_t kSamplingDisabled = ~uint64_t{0};
// Save-load callbacks can report success while scripts are still rebuilding
// the player's container changes.  Reading that linked list after only one
// second was intermittent: some loads completed in time, others died during
// the first inventory walk.  Require three seconds of the same player/cell and
// valid actor-value runtime before touching any variable-sized game data.
constexpr unsigned kStableWorldSamples = 30;
std::atomic<uint64_t> g_sample_not_before{kSamplingDisabled};
std::atomic<uint64_t> g_world_epoch{0};
std::atomic<PlayerUpdateFn> g_player_update_original{nullptr};
std::atomic<bool> g_player_update_hooked{false};
std::atomic<bool> g_first_player_update{true};
std::atomic<uint64_t> g_next_update_sample{0};
HANDLE g_worker = nullptr;
uint64_t g_sequence = 0;
dis::QuestState g_quest_cache[dis::kMaxQuests] = {};
uint32_t g_quest_cache_count = 0;
uint64_t g_next_quest_refresh = 0;
bool g_quest_cache_valid = false;
uint64_t g_quest_cache_signature = 0;
uint32_t g_logged_sanitized_quests[64] = {};
uint32_t g_logged_sanitized_quest_count = 0;
bool g_first_live_sample = true;
uint64_t g_last_summary_log = 0;
uint64_t g_last_transient_log = 0;
const char* g_last_transient_reason = nullptr;
dis::ItemState g_item_cache[dis::kMaxItems] = {};
uint32_t g_item_cache_count = 0;
float g_cached_carry = 0.0f;
int32_t g_cached_gold = 0;
bool g_inventory_cache_valid = false;
bool g_inventory_metadata_pending = false;
uint64_t g_inventory_cache_signature = 0;
uint64_t g_next_inventory_signature_ms = 0;
void* g_inventory_signature_base_form = nullptr;
uint64_t g_inventory_base_signature = 0;
uint32_t g_inventory_discovered_count = 0;
bool g_inventory_truncated = false;
uint32_t g_last_logged_item_count = UINT32_MAX;
bool g_last_logged_inventory_truncated = false;
uint32_t g_stable_left_hand_form = 0;
uint32_t g_stable_right_hand_form = 0;
uint32_t g_candidate_left_hand_form = 0;
uint32_t g_candidate_right_hand_form = 0;
unsigned g_candidate_hand_samples = 0;
bool g_stable_hands_initialized = false;
dis::MagicState g_magic_cache[dis::kMaxMagic] = {};
uint32_t g_magic_cache_count = 0;
uint32_t g_favorite_cache[dis::kMaxFavorites] = {};
uint32_t g_favorite_cache_count = 0;
uint32_t g_hotbar[dis::kMaxFavorites] = {};
bool g_hotbar_initialized = false;
uint32_t g_loaded_hotbar[dis::kMaxFavorites] = {};
bool g_loaded_hotbar_available = false;
constexpr uint32_t kMaxAllFavorites = 256;
uint32_t g_all_favorite_ids[kMaxAllFavorites] = {};
uint32_t g_all_favorite_count = 0;
bool g_magic_cache_valid = false;
bool g_magic_metadata_pending = false;
uint64_t g_magic_cache_signature = 0;
uint64_t g_next_magic_signature_ms = 0;
uint32_t g_last_logged_magic_count = UINT32_MAX;
uint32_t g_last_logged_favorite_count = UINT32_MAX;
bool g_last_tamriel_position_valid = false;
float g_last_tamriel_x = 0.0f;
float g_last_tamriel_y = 0.0f;
float g_last_tamriel_z = 0.0f;
dis::MapMarkerState g_map_marker_cache[dis::kMaxMapMarkers] = {};
uint32_t g_map_marker_cache_count = 0;
std::atomic<bool> g_map_marker_cache_valid{false};
std::atomic<bool> g_map_marker_refresh_requested{false};
uint64_t g_last_map_marker_refresh_ms = 0;
SRWLOCK g_map_marker_cache_lock = SRWLOCK_INIT;
uint32_t g_last_logged_marker_count = UINT32_MAX;
void* g_stable_player = nullptr;
void* g_stable_cell = nullptr;
unsigned g_stable_world_samples = 0;
std::atomic<DWORD> g_lifecycle_thread_id{0};
std::atomic<bool> g_logged_unsupported_magic_command{false};
int g_last_runtime_gate = -1;
SRWLOCK g_sample_lock = SRWLOCK_INIT;
thread_local bool g_sampling_on_player_update = false;

struct PerfMetric {
  uint64_t calls = 0;
  uint64_t total_us = 0;
  uint64_t max_us = 0;
};

PerfMetric g_perf_sample;
PerfMetric g_perf_inventory_signature;
PerfMetric g_perf_inventory_rebuild;
PerfMetric g_perf_map_signature;
PerfMetric g_perf_map_rebuild;
PerfMetric g_perf_magic_signature;
PerfMetric g_perf_magic_rebuild;
PerfMetric g_perf_submit;
uint64_t g_perf_window_started_ms = 0;

// Snapshot is about 700 KiB. Constructing it as a local inside
// PlayerCharacter's already-deep update stack touches enough guard pages to be
// unsafe under some Wine/Box64 thread-stack layouts. Keep the two
// process-lifetime work buffers in .bss and initialize the live buffer once per
// world rather than clearing it at the 10 Hz sample cadence.
dis::Snapshot g_sample_snapshot = {};
dis::Snapshot g_loading_snapshot = {};

struct PendingCharacterClone {
  void* clone = nullptr;
  uint64_t world_epoch = 0;
  uint64_t source_revision = 0;
  uint64_t appearance_signature = 0;
  float source_center[3] = {};
  float source_radius = 80.0f;
  uint32_t scene_objects = 0;
  uint32_t bounded_objects = 0;
  uint32_t geometry_objects = 0;
};
SRWLOCK g_pending_character_lock = SRWLOCK_INIT;
PendingCharacterClone g_pending_character = {};
std::atomic<uint64_t> g_character_reset_revision{1};
// The render thread never mutates the game-thread-owned appearance debounce
// fields directly. If it safely detaches a clone which failed UI3D adoption,
// this flag asks the next player-update sample to republish the same appearance
// instead of leaving g_character_published_signature latched forever.
std::atomic<bool> g_character_clone_retry_requested{false};
uint64_t g_character_candidate_signature = 0;
unsigned g_character_candidate_samples = 0;
uint64_t g_character_published_signature = 0;
uint64_t g_character_next_clone_ms = 0;
uint64_t g_character_last_source_log_ms = 0;
// Equipment changes use a short sequence of real post-Actor::Update clones so
// the bottom screen shows Skyrim's evaluated draw/sheath animation instead of
// dropping the old portrait and waiting for a final idle pose. Only one clone
// can be pending/attached/in-flight at a time; Present retires it before the
// next sample is allowed to publish.
constexpr uint64_t kCharacterEquipmentAnimationCaptureMs = 2400;
constexpr uint64_t kCharacterEquipmentAnimationFrameMs = 33;
std::atomic<uint64_t> g_character_live_transition_until_ms{0};
std::atomic<uint64_t> g_character_source_revision{0};
// Render-thread adoption is published separately from the game-thread
// signature latch. A matching signature is only considered complete while an
// actual pending/attached clone still owns it; otherwise the sampler can
// recover automatically from a lost publication.
std::atomic<uint64_t> g_character_adopted_signature{0};
std::atomic<uint64_t> g_character_cached_signature{0};
std::atomic<uint64_t> g_character_cached_world_epoch{0};
// Bridges the short Present->HUD query interval after the shallow actor clone
// has already been retired. The game-thread sampler treats this exact frame as
// a live publication and therefore does not create a redundant replacement.
std::atomic<uint64_t> g_character_inflight_signature{0};
std::atomic<uint64_t> g_character_inflight_world_epoch{0};
std::atomic<bool> g_character_surface_visible{false};
std::atomic<bool> g_character_interacting{false};
std::atomic<uint64_t> g_character_empty_suspended_signature{0};

using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
std::atomic<PresentFn> g_capture_present_original{nullptr};
std::atomic<bool> g_capture_present_hooked{false};
using HudPostDisplayFn = void(*)(void*);
std::atomic<HudPostDisplayFn> g_hud_post_display_original{nullptr};
std::atomic<bool> g_hud_post_display_hooked{false};
using InventoryPostDisplayFn = void(*)(void*);
std::atomic<InventoryPostDisplayFn> g_inventory_post_display_original{nullptr};
std::atomic<bool> g_inventory_post_display_hooked{false};
using MainRenderWorldFn = void(*)(bool);
std::atomic<MainRenderWorldFn> g_main_render_world_original{nullptr};
std::atomic<bool> g_main_render_world_hooked{false};
void* g_main_render_world_relay = nullptr;
std::atomic<uint32_t> g_native_inventory_control_traces{0};
std::atomic<uint32_t> g_native_item_resource_traces{0};
std::atomic<uint32_t> g_native_magic_resource_traces{0};
std::atomic<uint32_t> g_native_item_render_diagnostic_frames{0};
// InventoryMenu mutates the player's equipment graph through native paths
// which do not pass through WEMU's command queue. Its PostDisplay callback is
// an authoritative, allocation-free signal that the menu owns those paths.
// Keep actor-clone creation quarantined while callbacks are current.
std::atomic<uint64_t> g_native_inventory_last_display_ms{0};
std::atomic<IDXGISwapChain*> g_primary_capture_chain{nullptr};
std::atomic<uint64_t> g_native_render_reset_revision{1};
std::atomic<uint64_t> g_ui3d_gate_scopes{0};
std::atomic<uint64_t> g_ui3d_gate_failures{0};
SRWLOCK g_native_render_lock = SRWLOCK_INIT;
// Equipment changes rebuild pieces of the live actor graph. Keep native actor
// adoption/rendering disabled for the whole engine mutation, while holding the
// non-recursive render lock only for the short clone-detach transaction.
std::atomic<bool> g_equipment_mutation_in_progress{false};
// ActorEquipManager queues part of an equip/unequip operation for the next
// PlayerCharacter updates.  Returning from EquipObject/UnequipObject is not a
// graph-lifetime boundary: restarting Inventory3DManager immediately there
// raced the queued actor rebuild and was the reproducible unequip crash.  Keep
// every native/custom preview transaction quiescent until the post-update live
// equipment state has matched the requested result for two consecutive frames.
struct EquipmentSettlementState {
  dis::Command command{};
  uint64_t started_ms = 0;
  uint32_t post_update_frames = 0;
  uint32_t confirmed_frames = 0;
  bool expected_equipped = false;
  bool track_live_equipped_state = false;
};
std::atomic<bool> g_equipment_settlement_active{false};
EquipmentSettlementState g_equipment_settlement{};
// External/native equipment changes are detected on PlayerCharacter::Update.
// Publish the transition before taking the render lock so HUD PostDisplay can
// never adopt/render a replacement while retirement is waiting to run.
std::atomic<bool> g_character_transition_pending{false};
uint64_t g_observed_native_render_reset_revision = 0;
bool g_native_render_reset_pending = false;
bool g_native_render_reset_resources_retired = false;
bool g_native_render_reset_timeout_logged = false;
uint64_t g_native_render_reset_started_ms = 0;
thread_local bool g_inside_capture_present = false;
thread_local bool g_inside_hud_post_display = false;
thread_local bool g_inside_inventory_post_display = false;
thread_local bool g_inside_native_map_world_render = false;
// The proxy command lane is bounded to 64 and the UI requests only the selected
// detail model. Matching that bound avoids normalizing/zeroing a 896-entry
// array and placing a ~29 KiB purge buffer on Skyrim's render-thread stack.
constexpr unsigned kNativePreviewQueueCapacity = 64;
constexpr uint64_t kNativePreviewOwnerWaitLimitMs = 5000;
constexpr uint32_t kNativePreviewOwnerDeferralLimit = 50;
struct NativePreviewRequest {
  uint32_t form_id = 0;
  uint32_t owner_deferrals = 0;
  uint64_t world_epoch = 0;
  // Starts only when this request actually collides with a non-neutral native
  // Inventory3DManager. Time spent waiting behind other WEMU previews must not
  // consume the native-owner retry budget.
  uint64_t owner_wait_started_ms = 0;
  // Foreground detail-card requests may bypass background cache warming, but
  // preserve FIFO ordering among foreground requests so later row thumbnails
  // cannot push the actively selected model to the back again.
  bool priority = false;
};
SRWLOCK g_native_preview_queue_lock = SRWLOCK_INIT;
NativePreviewRequest g_native_preview_queue[kNativePreviewQueueCapacity] = {};
NativePreviewRequest
    g_native_preview_queue_scratch[kNativePreviewQueueCapacity] = {};
unsigned g_native_preview_queue_read = 0;
unsigned g_native_preview_queue_write = 0;
unsigned g_native_preview_queue_count = 0;
std::atomic<unsigned> g_native_preview_queue_depth{0};
std::atomic<uint32_t> g_native_active_preview_form{0};
std::atomic<uint64_t> g_native_active_preview_epoch{0};
// A UI tap must not be lost merely because the character renderer owns a
// one-frame GPU lease. Equipment commands are retried on the next player update
// after that bounded lease/query is quiescent.
bool g_deferred_equipment_command_valid = false;
dis::Command g_deferred_equipment_command = {};
dis::Command g_last_action_command = {};
uint64_t g_last_action_command_ms = 0;
std::atomic<bool> g_native_capture_equipment_preempt_requested{false};
uint8_t g_native_preview_rgba[
    dis::kNativeItemPreviewSize * dis::kNativeItemPreviewSize * 4] = {};

struct NativeCharacterResources {
  ID3D11Device* device = nullptr;
  ID3D11Texture2D* render_texture = nullptr;
  ID3D11RenderTargetView* render_view = nullptr;
  ID3D11ShaderResourceView* shader_view = nullptr;
  ID3D11Texture2D* depth_texture = nullptr;
  ID3D11DepthStencilView* depth_view = nullptr;
  ID3D11Texture2D* depth_staging = nullptr;
  ID3D11Texture2D* staging = nullptr;
  ID3D11Query* readback_query = nullptr;
  uint32_t width = 0;
  uint32_t height = 0;
  // The native UI3D pass renders with a 3x overscan projection so a deliberately
  // reduced clone can be captured without clipping and still retain the
  // texture detail needed by the final panel-sized image.
  uint32_t output_width = 0;
  uint32_t output_height = 0;
  float output_zoom = 1.0f;
  float primary_aspect = 1.0f;
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  void* active_clone = nullptr;
  void* attached_root = nullptr;
  void* scheme_manager = nullptr;
  void* scheme_root = nullptr;
  bool attached = false;
  bool scheme_session_owned = false;
  // Set only after UI3D submission successfully arms the deferred framebuffer
  // lease. Present clears it by safely detaching the exact submitted clone;
  // it remains set across transient manager/readback failures so retirement is
  // retried instead of leaving a shallow actor graph resident indefinitely.
  bool submitted_scene_pending_retirement = false;
  bool has_good_frame = false;
  // The RGBA portrait outlives the short-lived Skyrim scene clone which
  // produced it. Keeping this identity separate from active_* lets us release
  // every shallow clone as soon as its GPU readback is complete, so no clone
  // can survive a later ActorEquipManager graph rebuild.
  uint64_t cached_world_epoch = 0;
  uint64_t cached_source_revision = 0;
  uint64_t cached_appearance_signature = 0;
  bool pose_initialized = false;
  uint64_t active_world_epoch = 0;
  uint64_t active_source_revision = 0;
  uint64_t observed_reset_revision = 0;
  uint64_t last_render_ms = 0;
  uint64_t last_control_revision = 0;
  uint64_t refresh_request_control_revision = UINT64_MAX;
  int32_t refresh_request_width = 0;
  int32_t refresh_request_height = 0;
  uint64_t last_failure_log_ms = 0;
  uint64_t last_diagnostic_log_ms = 0;
  uint32_t scene_objects = 0;
  uint32_t bounded_objects = 0;
  uint32_t geometry_objects = 0;
  uint32_t foreground_pixels = 0;
  uint32_t content_min_x = 0;
  uint32_t content_min_y = 0;
  uint32_t content_max_x = 0;
  uint32_t content_max_y = 0;
  uint8_t minimum_rgb = 0;
  uint8_t maximum_rgb = 0;
  uint32_t engine_render_result = 0;
  uint64_t render_attempts = 0;
  uint64_t render_successes = 0;
  uint64_t render_empty_frames = 0;
  uint32_t consecutive_empty_frames = 0;
  bool empty_render_suspended = false;
  bool readback_pending = false;
  bool readback_copy_issued = false;
  bool readback_ready = false;
  bool readback_success = false;
  uint64_t readback_dispatched_ms = 0;
  uint64_t readback_world_epoch = 0;
  uint64_t readback_source_revision = 0;
  uint64_t readback_appearance_signature = 0;
  uint64_t readback_reset_revision = 0;
  uint32_t reset_detach_attempts = 0;
  uint64_t reset_detach_next_ms = 0;
  bool reset_detach_exhausted_logged = false;
  float source_center[3] = {};
  float source_radius = 80.0f;
  // Inventory3DManager's native model/light anchor. The actor and its private
  // camera move together here so Skyrim's exact scheme-1 lights illuminate
  // the original skin/armor materials without touching the gameplay camera.
  float inventory_light_anchor[3] = {-29.0f, -500.0f, 12.0f};
  float camera_fit_margin = 1.35f;
  uint32_t camera_fit_retries = 0;
  float camera_pan_x = 0.0f;
  float camera_pan_z = 0.0f;
  float camera_last_distance = 0.0f;
  float camera_tangent_horizontal = 0.0f;
  float camera_tangent_vertical = 0.0f;
  float camera_expected_width = 0.0f;
  float camera_expected_height = 0.0f;
  uint32_t camera_pan_corrections = 0;
  uint32_t camera_centered_confirmations = 0;
  float camera_bounds_minimum[3] = {};
  float camera_bounds_maximum[3] = {};
  uint32_t camera_bounds_visited = 0;
  uint32_t camera_bounds_geometry = 0;
  uint64_t camera_bounds_control_revision = 0;
  bool camera_bounds_valid = false;
  // Presentation crop is derived once from the actor's rotation-invariant
  // root bound and then held fixed. Re-centering on each yaw silhouette made
  // the apparent camera fight the user's drag input.
  uint32_t stable_crop_min_x = 0;
  uint32_t stable_crop_min_y = 0;
  uint32_t stable_crop_max_x = 0;
  uint32_t stable_crop_max_y = 0;
  bool stable_crop_valid = false;
};
NativeCharacterResources g_character_render;
uint8_t g_character_frame_rgba[
    dis::kCharacterPreviewMaxWidth * dis::kCharacterPreviewMaxHeight * 4] = {};
constexpr uint32_t kCharacterCaptureScale = 3;
uint8_t g_character_capture_rgba[
    dis::kCharacterPreviewMaxWidth * kCharacterCaptureScale *
    dis::kCharacterPreviewMaxHeight * kCharacterCaptureScale * 4] = {};
// Depth-coverage histograms let the portrait crop distinguish the dense actor
// from a few distant pixels on a drawn sword, bow, staff, or spell effect.
// Process-lifetime storage keeps these arrays off Skyrim's render-thread stack.
uint32_t g_character_depth_columns[
    dis::kCharacterPreviewMaxWidth * kCharacterCaptureScale] = {};
uint32_t g_character_depth_rows[
    dis::kCharacterPreviewMaxHeight * kCharacterCaptureScale] = {};

struct NativeMapCaptureResources {
  ID3D11Device* device = nullptr;
  ID3D11Texture2D* render_texture = nullptr;
  ID3D11RenderTargetView* render_view = nullptr;
  ID3D11ShaderResourceView* shader_view = nullptr;
  ID3D11Texture2D* depth_texture = nullptr;
  ID3D11DepthStencilView* depth_view = nullptr;
  ID3D11Texture2D* staging = nullptr;
  ID3D11Query* readback_query = nullptr;
  ID3D11Query* pipeline_query = nullptr;
  ID3D11Query* occlusion_query = nullptr;
  IDXGISwapChain* chain = nullptr;
  // The map owns a fixed, private offscreen surface.  These are never primary
  // swapchain dimensions and the map readback never accepts the gameplay back
  // buffer as a source.
  uint32_t capture_width = 0;
  uint32_t capture_height = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  uint64_t active_world_epoch = 0;
  uint64_t readback_world_epoch = 0;
  uint32_t readback_world_id = 0;
  uint64_t readback_request_id = 0;
  uint64_t readback_control_revision = 0;
  uint64_t readback_dispatched_ms = 0;
  uint64_t readback_dispatched_us = 0;
  uint64_t last_setup_us = 0;
  uint64_t last_render_us = 0;
  uint64_t last_pixel_hash = 0;
  uint64_t last_dispatch_ms = 0;
  uint64_t source_revision = 0;
  uint64_t captured_frames = 0;
  uint64_t failed_frames = 0;
  uint64_t stale_frames = 0;
  uint64_t transition_frames = 0;
  uint64_t last_log_ms = 0;
  uint64_t last_failure_log_ms = 0;
  dis::ProjectedMapMarker projected_markers[dis::kMaxMapMarkers] = {};
  uint32_t projected_marker_count = 0;
  bool readback_pending = false;
  bool readback_copy_issued = false;
};
NativeMapCaptureResources g_map_capture;
std::atomic<uint64_t> g_native_map_requested_until_ms{0};
std::atomic<uint64_t> g_native_map_request_serial{0};
std::atomic<uint64_t> g_native_map_active_request{0};
std::atomic<uint64_t> g_native_map_request_started_ms{0};
std::atomic<uint64_t> g_native_map_terminal_request{0};
std::atomic<uint64_t> g_native_map_lock_misses{0};
std::atomic<uint64_t> g_native_map_failure_count{0};
std::atomic<uint64_t> g_native_map_request_failure_base{0};
std::atomic<const char*> g_native_map_last_failure_reason{nullptr};
std::atomic<long> g_native_map_last_failure_hr{S_OK};
std::atomic<float> g_native_map_center_x{0.0f};
std::atomic<float> g_native_map_center_y{0.0f};
std::atomic<float> g_native_map_center_z{0.0f};
std::atomic<bool> g_native_map_center_initialized{false};
std::atomic<uint32_t> g_native_map_world_id{0};
// Live player marker data is read by the render hook without retaining a
// PlayerCharacter pointer. It only affects the overlay and never recenters or
// writes either the private map camera or PlayerCamera.
std::atomic<float> g_native_map_player_x{0.0f};
std::atomic<float> g_native_map_player_y{0.0f};
std::atomic<float> g_native_map_player_z{0.0f};
std::atomic<float> g_native_map_player_heading{0.0f};
// ResolvePosition consults the live player every time it is called. Resolve
// the initial world anchor once, then retain only POD camera state so later
// lower-screen captures cannot follow the player or gameplay camera.
std::atomic<float> g_native_map_resolved_x{0.0f};
std::atomic<float> g_native_map_resolved_y{0.0f};
std::atomic<float> g_native_map_resolved_z{0.0f};
std::atomic<bool> g_native_map_anchor_resolved{false};
std::atomic<float> g_native_map_view_offset_x{0.0f};
std::atomic<float> g_native_map_view_offset_y{0.0f};
std::atomic<float> g_native_map_view_offset_z{0.0f};
std::atomic<float> g_native_map_view_zoom{0.25f};
std::atomic<bool> g_native_map_view_initialized{false};
std::atomic<float> g_native_map_pending_pan_x{0.0f};
std::atomic<float> g_native_map_pending_pan_y{0.0f};
std::atomic<float> g_native_map_pending_zoom{0.0f};
std::atomic<uint64_t> g_native_map_control_revision{0};
std::atomic<uint64_t> g_native_map_applied_control_revision{0};
std::atomic<uint32_t> g_native_map_render_diagnostic_frames{0};
uint8_t g_map_frame_rgba[
    dis::kNativeMapMaxWidth * dis::kNativeMapMaxHeight * 4] = {};

void AccumulateNativeMapInput(std::atomic<float>& destination, float delta,
                              float absolute_limit) {
  if (!std::isfinite(delta) || !std::isfinite(absolute_limit) ||
      absolute_limit <= 0.0f)
    return;
  float current = destination.load(std::memory_order_acquire);
  for (;;) {
    const float next = std::clamp(
        (std::isfinite(current) ? current : 0.0f) + delta,
        -absolute_limit, absolute_limit);
    if (destination.compare_exchange_weak(
            current, next, std::memory_order_acq_rel,
            std::memory_order_acquire))
      return;
  }
}

void PublishNativeMapControlRevision(uint64_t revision) {
  uint64_t current =
      g_native_map_control_revision.load(std::memory_order_acquire);
  while (current < revision &&
         !g_native_map_control_revision.compare_exchange_weak(
             current, revision, std::memory_order_acq_rel,
             std::memory_order_acquire)) {}
}

bool NativeMapInputPending() {
  const float pan_x =
      g_native_map_pending_pan_x.load(std::memory_order_acquire);
  const float pan_y =
      g_native_map_pending_pan_y.load(std::memory_order_acquire);
  const float zoom =
      g_native_map_pending_zoom.load(std::memory_order_acquire);
  return (std::isfinite(pan_x) && std::abs(pan_x) > 0.000001f) ||
         (std::isfinite(pan_y) && std::abs(pan_y) > 0.000001f) ||
         (std::isfinite(zoom) && std::abs(zoom) > 0.000001f);
}

void ReleaseNiObject(void* object);
bool ReleaseCharacterCloneSafely(void* clone, const char* reason);

struct SceneTransform {
  float rotate[9] = {};
  float translate[3] = {};
  float scale = 1.0f;
};

void* SceneVirtualPointer(void* object, size_t slot);
bool ReadSceneTransform(void* object, SceneTransform* transform);

SRWLOCK g_plugin_log_lock = SRWLOCK_INIT;
HANDLE g_plugin_log_file = INVALID_HANDLE_VALUE;
wchar_t g_plugin_log_path[MAX_PATH] = {};
DWORD g_plugin_log_last_flush_ms = 0;
std::atomic<bool> g_character_clone_release_probe{false};
std::atomic<void*> g_character_clone_release_object{nullptr};
std::atomic<DWORD> g_character_clone_release_thread{0};
std::atomic_flag g_character_clone_exception_reporting = ATOMIC_FLAG_INIT;
PVOID g_character_clone_exception_handler = nullptr;

#if defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
void PluginLog(bool reset, const char* format, ...) {
  char message[430] = {};
  va_list args;
  va_start(args, format);
  int message_used = std::vsnprintf(message, sizeof(message) - 1, format, args);
  va_end(args);
  if (message_used < 0) message_used = 0;
  SYSTEMTIME time = {};
  GetLocalTime(&time);
  char line[512] = {};
  int used = std::snprintf(
      line, sizeof(line) - 3, "%04u-%02u-%02u %02u:%02u:%02u.%03u [tid=%lu] %s",
      time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
      time.wSecond, time.wMilliseconds,
      static_cast<unsigned long>(GetCurrentThreadId()), message);
  if (used < 0) used = 0;
  used = std::min<int>(used, static_cast<int>(sizeof(line) - 3));
  line[used++] = '\r';
  line[used++] = '\n';

  AcquireSRWLockExclusive(&g_plugin_log_lock);
  if (!g_plugin_log_path[0]) {
    const DWORD n = GetModuleFileNameW(nullptr, g_plugin_log_path, MAX_PATH);
    wchar_t* slash = n && n < MAX_PATH - 40
                         ? wcsrchr(g_plugin_log_path, L'\\')
                         : nullptr;
    if (!slash) {
      g_plugin_log_path[0] = 0;
    } else {
      wcscpy_s(slash + 1,
               static_cast<size_t>(MAX_PATH -
                                   (slash + 1 - g_plugin_log_path)),
               L"SkyrimDualScreenPlugin.log");
    }
  }
  if (reset && g_plugin_log_file != INVALID_HANDLE_VALUE) {
    FlushFileBuffers(g_plugin_log_file);
    CloseHandle(g_plugin_log_file);
    g_plugin_log_file = INVALID_HANDLE_VALUE;
  }
  if (reset && g_plugin_log_path[0]) {
    wchar_t previous[MAX_PATH] = {};
    wcscpy_s(previous, MAX_PATH, g_plugin_log_path);
    wcscat_s(previous, MAX_PATH, L".prev");
    DeleteFileW(previous);
    MoveFileW(g_plugin_log_path, previous);
  }
  if (g_plugin_log_file == INVALID_HANDLE_VALUE && g_plugin_log_path[0]) {
    g_plugin_log_file = CreateFileW(
        g_plugin_log_path, FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        reset ? CREATE_ALWAYS : OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    g_plugin_log_last_flush_ms = GetTickCount();
  }
  DWORD written = 0;
  if (g_plugin_log_file != INVALID_HANDLE_VALUE) {
    WriteFile(g_plugin_log_file, line, static_cast<DWORD>(used), &written,
              nullptr);
    const DWORD now = GetTickCount();
    const bool urgent = std::strstr(message, "failed") != nullptr ||
                        std::strstr(message, "exception") != nullptr ||
                        std::strstr(message, "crash") != nullptr;
    // Android shared storage makes open/close/flush per line surprisingly
    // expensive on the game and Present threads. Keep one process-lifetime
    // handle and make routine diagnostics durable in one-second batches.
    if (reset || urgent || now - g_plugin_log_last_flush_ms >= 1000) {
      FlushFileBuffers(g_plugin_log_file);
      g_plugin_log_last_flush_ms = now;
    }
  }
  ReleaseSRWLockExclusive(&g_plugin_log_lock);
}

uint64_t PerfNowUs() {
  static const uint64_t frequency = []() -> uint64_t {
    LARGE_INTEGER value = {};
    return QueryPerformanceFrequency(&value) && value.QuadPart > 0
               ? static_cast<uint64_t>(value.QuadPart)
               : 1u;
  }();
  LARGE_INTEGER value = {};
  QueryPerformanceCounter(&value);
  return static_cast<uint64_t>(value.QuadPart) * 1000000ull / frequency;
}

void RecordPerf(PerfMetric& metric, uint64_t started_us) {
  const uint64_t elapsed = PerfNowUs() - started_us;
  ++metric.calls;
  metric.total_us += elapsed;
  metric.max_us = std::max(metric.max_us, elapsed);
}

uint64_t PerfAverage(const PerfMetric& metric) {
  return metric.calls ? metric.total_us / metric.calls : 0;
}

void ResetSamplerPerfWindow(uint64_t now_ms) {
  g_perf_sample = {};
  g_perf_inventory_signature = {};
  g_perf_inventory_rebuild = {};
  g_perf_map_signature = {};
  g_perf_map_rebuild = {};
  g_perf_magic_signature = {};
  g_perf_magic_rebuild = {};
  g_perf_submit = {};
  g_perf_window_started_ms = now_ms;
}

void MaybeLogSamplerPerf(uint64_t now_ms) {
  if (!g_perf_window_started_ms) {
    g_perf_window_started_ms = now_ms;
    return;
  }
  if (now_ms - g_perf_window_started_ms < 10000) return;
  const uint64_t window_ms = now_ms - g_perf_window_started_ms;
  PluginLog(false,
            "[SKYRIM-PERF] thread=%lu window_ms=%llu samples=%llu "
            "sample_us={avg=%llu max=%llu} submit_us={calls=%llu avg=%llu "
            "max=%llu}",
            static_cast<unsigned long>(GetCurrentThreadId()),
            static_cast<unsigned long long>(window_ms),
            static_cast<unsigned long long>(g_perf_sample.calls),
            static_cast<unsigned long long>(PerfAverage(g_perf_sample)),
            static_cast<unsigned long long>(g_perf_sample.max_us),
            static_cast<unsigned long long>(g_perf_submit.calls),
            static_cast<unsigned long long>(PerfAverage(g_perf_submit)),
            static_cast<unsigned long long>(g_perf_submit.max_us));
  PluginLog(false,
            "[SKYRIM-PERF-INVENTORY] checks=%llu cache_hits=%llu "
            "rebuilds=%llu signature_us={avg=%llu max=%llu} "
            "rebuild_us={avg=%llu max=%llu}",
            static_cast<unsigned long long>(g_perf_inventory_signature.calls),
            static_cast<unsigned long long>(
                g_perf_inventory_signature.calls >=
                        g_perf_inventory_rebuild.calls
                    ? g_perf_inventory_signature.calls -
                          g_perf_inventory_rebuild.calls
                    : 0),
            static_cast<unsigned long long>(g_perf_inventory_rebuild.calls),
            static_cast<unsigned long long>(
                PerfAverage(g_perf_inventory_signature)),
            static_cast<unsigned long long>(g_perf_inventory_signature.max_us),
            static_cast<unsigned long long>(
                PerfAverage(g_perf_inventory_rebuild)),
            static_cast<unsigned long long>(g_perf_inventory_rebuild.max_us));
  PluginLog(false,
            "[SKYRIM-PERF-MAP] checks=%llu cache_hits=%llu rebuilds=%llu "
            "signature_us={avg=%llu max=%llu} rebuild_us={avg=%llu max=%llu}",
            static_cast<unsigned long long>(g_perf_map_signature.calls),
            static_cast<unsigned long long>(
                g_perf_map_signature.calls >= g_perf_map_rebuild.calls
                    ? g_perf_map_signature.calls - g_perf_map_rebuild.calls
                    : 0),
            static_cast<unsigned long long>(g_perf_map_rebuild.calls),
            static_cast<unsigned long long>(PerfAverage(g_perf_map_signature)),
            static_cast<unsigned long long>(g_perf_map_signature.max_us),
            static_cast<unsigned long long>(PerfAverage(g_perf_map_rebuild)),
            static_cast<unsigned long long>(g_perf_map_rebuild.max_us));
  PluginLog(false,
            "[SKYRIM-PERF-MAGIC] checks=%llu cache_hits=%llu rebuilds=%llu "
            "signature_us={avg=%llu max=%llu} rebuild_us={avg=%llu max=%llu}",
            static_cast<unsigned long long>(g_perf_magic_signature.calls),
            static_cast<unsigned long long>(
                g_perf_magic_signature.calls >= g_perf_magic_rebuild.calls
                    ? g_perf_magic_signature.calls -
                          g_perf_magic_rebuild.calls
                    : 0),
            static_cast<unsigned long long>(g_perf_magic_rebuild.calls),
            static_cast<unsigned long long>(
                PerfAverage(g_perf_magic_signature)),
            static_cast<unsigned long long>(g_perf_magic_signature.max_us),
            static_cast<unsigned long long>(PerfAverage(g_perf_magic_rebuild)),
            static_cast<unsigned long long>(g_perf_magic_rebuild.max_us));
  ResetSamplerPerfWindow(now_ms);
}

constexpr uint32_t kSerializationUniqueId = 0x554D4557u;  // "WEMU"
constexpr uint32_t kHotbarRecordType = 0x42544F48u;       // "HOTB"
constexpr uint32_t kHotbarRecordVersion = 1;

struct HotbarRecord {
  uint32_t slots[dis::kMaxFavorites] = {};
};

void OnSerializationSave(SKSESerializationInterface* serialization) {
  if (!serialization || !serialization->WriteRecord) return;
  HotbarRecord record = {};
  bool initialized = false;
  AcquireSRWLockShared(&g_sample_lock);
  initialized = g_hotbar_initialized;
  if (initialized)
    std::copy_n(g_hotbar, dis::kMaxFavorites, record.slots);
  ReleaseSRWLockShared(&g_sample_lock);
  if (!initialized) return;
  const bool saved = serialization->WriteRecord(
      kHotbarRecordType, kHotbarRecordVersion, &record, sizeof(record));
  PluginLog(false, "hotbar: serialization save success=%u", saved ? 1u : 0u);
}

void OnSerializationLoad(SKSESerializationInterface* serialization) {
  if (!serialization || !serialization->GetNextRecordInfo ||
      !serialization->ReadRecordData)
    return;
  HotbarRecord loaded = {};
  bool found = false;
  uint32_t type = 0;
  uint32_t version = 0;
  uint32_t length = 0;
  while (serialization->GetNextRecordInfo(&type, &version, &length)) {
    if (type != kHotbarRecordType || version != kHotbarRecordVersion ||
        length != sizeof(HotbarRecord))
      continue;
    if (serialization->ReadRecordData(&loaded, sizeof(loaded)) !=
        sizeof(loaded))
      continue;
    found = true;
    break;
  }
  if (found && serialization->ResolveFormId) {
    for (uint32_t& form_id : loaded.slots) {
      if (!form_id) continue;
      uint32_t resolved = 0;
      if (!serialization->ResolveFormId(form_id, &resolved)) resolved = 0;
      form_id = resolved;
    }
  }
  AcquireSRWLockExclusive(&g_sample_lock);
  std::copy_n(loaded.slots, dis::kMaxFavorites, g_loaded_hotbar);
  g_loaded_hotbar_available = found;
  ReleaseSRWLockExclusive(&g_sample_lock);
  PluginLog(false, "hotbar: serialization load found=%u", found ? 1u : 0u);
}

void OnSerializationRevert(SKSESerializationInterface*) {
  AcquireSRWLockExclusive(&g_sample_lock);
  std::fill_n(g_loaded_hotbar, dis::kMaxFavorites, 0u);
  g_loaded_hotbar_available = false;
  ReleaseSRWLockExclusive(&g_sample_lock);
  PluginLog(false, "hotbar: serialization reverted");
}

void CompleteNativePreviewFailure(uint32_t form_id, uint64_t world_epoch,
                                  const char* reason) {
  if (!form_id) return;
  PluginLog(false,
            "[ITEM_RENDER] failed form=%08X epoch=%llu reason=%s",
            form_id, static_cast<unsigned long long>(world_epoch),
            reason ? reason : "unspecified");
  if (g_submit_item_preview)
    g_submit_item_preview(form_id, world_epoch, 0, 0, nullptr, 0);
}

void LoadNativeRenderingOptions() {
  wchar_t ini[MAX_PATH] = {};
  const DWORD length = GetModuleFileNameW(nullptr, ini, MAX_PATH);
  if (!length || length >= MAX_PATH) return;
  wchar_t* slash = wcsrchr(ini, L'\\');
  if (!slash) return;
  wcscpy_s(slash + 1,
           static_cast<size_t>(MAX_PATH - (slash + 1 - ini)),
           L"SkyrimSecondScreen.ini");
  const bool safe_mode =
      GetPrivateProfileIntW(L"ui", L"safe_mode", 0, ini) != 0;
  const bool requested_items =
      GetPrivateProfileIntW(L"ui", L"enable_native_item_models", 1, ini) != 0;
  const bool requested_character =
      GetPrivateProfileIntW(L"ui", L"enable_character_preview", 1, ini) != 0;
  const bool requested_map =
      GetPrivateProfileIntW(L"ui", L"enable_native_map", 1, ini) != 0;
  g_native_item_models_enabled = !safe_mode && requested_items;
  // Character capture is intentionally retired. Keeping this false prevents
  // the renderer hook from cloning or attaching the player graph even when a
  // stale INI from an older build still contains enable_character_preview=1.
  g_native_character_preview_enabled = false;
  g_custom_character_preview_enabled = !safe_mode && requested_character;
  g_native_map_enabled = !kNativeMapTemporarilyDisabled && !safe_mode &&
                         requested_map;
  PluginLog(false,
            "config: safe_mode=%u requested(items=%u character=%u map=%u) "
            "effective(items=%u character_custom=%u character_legacy=%u map=%u)",
            safe_mode ? 1u : 0u,
            requested_items ? 1u : 0u,
            requested_character ? 1u : 0u,
            requested_map ? 1u : 0u,
            g_native_item_models_enabled ? 1u : 0u,
            g_custom_character_preview_enabled ? 1u : 0u,
            g_native_character_preview_enabled ? 1u : 0u,
            g_native_map_enabled ? 1u : 0u);
}

void ResetLiveCaches() {
  // The live snapshot owns persistent copies of the large inventory, magic,
  // quest, and (currently empty) marker sections. Clear it at the world
  // boundary, never from the 10 Hz sampler. The first actual section rebuild
  // below will advance that section's revision from zero.
  g_sample_snapshot = {};
  g_sample_snapshot.version = dis::kBridgeVersion;
  g_sample_snapshot.size = sizeof(dis::Snapshot);

  // Preview requests are world-bound.  Never allow an unprocessed request
  // from the previous save to resolve the same FormID in the next world.
  NativePreviewRequest dropped_requests[kNativePreviewQueueCapacity] = {};
  unsigned dropped_request_count = 0;
  AcquireSRWLockExclusive(&g_native_preview_queue_lock);
  for (unsigned i = 0; i < g_native_preview_queue_count; ++i) {
    const unsigned at = (g_native_preview_queue_read + i) %
                        kNativePreviewQueueCapacity;
    dropped_requests[dropped_request_count++] = g_native_preview_queue[at];
  }
  for (auto& request : g_native_preview_queue) request = {};
  g_native_preview_queue_read = 0;
  g_native_preview_queue_write = 0;
  g_native_preview_queue_count = 0;
  g_native_preview_queue_depth.store(0, std::memory_order_release);
  ReleaseSRWLockExclusive(&g_native_preview_queue_lock);
  for (unsigned i = 0; i < dropped_request_count; ++i)
    CompleteNativePreviewFailure(dropped_requests[i].form_id,
                                 dropped_requests[i].world_epoch,
                                 "world reset queue drop");
  void* pending_character = nullptr;
  AcquireSRWLockExclusive(&g_pending_character_lock);
  pending_character = g_pending_character.clone;
  g_pending_character = {};
  ReleaseSRWLockExclusive(&g_pending_character_lock);
  if (pending_character)
    ReleaseCharacterCloneSafely(pending_character, "world reset pending clone");
  g_character_candidate_signature = 0;
  g_character_candidate_samples = 0;
  g_character_published_signature = 0;
  g_character_next_clone_ms = 0;
  g_character_last_source_log_ms = 0;
  g_character_live_transition_until_ms.store(0,
                                               std::memory_order_release);
  g_character_clone_retry_requested.store(false, std::memory_order_release);
  g_character_adopted_signature.store(0, std::memory_order_release);
  g_character_cached_signature.store(0, std::memory_order_release);
  g_character_cached_world_epoch.store(0, std::memory_order_release);
  g_character_inflight_signature.store(0, std::memory_order_release);
  g_character_inflight_world_epoch.store(0, std::memory_order_release);
  g_character_surface_visible.store(false, std::memory_order_release);
  g_character_empty_suspended_signature.store(0,
                                               std::memory_order_release);
  g_character_transition_pending.store(false, std::memory_order_release);
  g_native_inventory_last_display_ms.store(0, std::memory_order_release);
  g_deferred_equipment_command = {};
  g_deferred_equipment_command_valid = false;
  g_last_action_command = {};
  g_last_action_command_ms = 0;
  g_equipment_settlement = {};
  g_equipment_settlement_active.store(false, std::memory_order_release);
  g_native_capture_equipment_preempt_requested.store(
      false, std::memory_order_release);
  g_character_reset_revision.fetch_add(1, std::memory_order_acq_rel);
  if (g_set_character_ready) g_set_character_ready(0);
  g_quest_cache_valid = false;
  g_quest_cache_count = 0;
  g_quest_cache_signature = 0;
  g_next_quest_refresh = 0;
  g_inventory_cache_valid = false;
  g_inventory_metadata_pending = false;
  g_inventory_cache_signature = 0;
  g_next_inventory_signature_ms = 0;
  g_inventory_signature_base_form = nullptr;
  g_inventory_base_signature = 0;
  g_item_cache_count = 0;
  g_cached_carry = 0.0f;
  g_cached_gold = 0;
  g_inventory_discovered_count = 0;
  g_inventory_truncated = false;
  g_last_logged_item_count = UINT32_MAX;
  g_last_logged_inventory_truncated = false;
  g_stable_left_hand_form = 0;
  g_stable_right_hand_form = 0;
  g_candidate_left_hand_form = 0;
  g_candidate_right_hand_form = 0;
  g_candidate_hand_samples = 0;
  g_stable_hands_initialized = false;
  g_magic_cache_valid = false;
  g_magic_metadata_pending = false;
  g_magic_cache_signature = 0;
  g_next_magic_signature_ms = 0;
  g_magic_cache_count = 0;
  g_favorite_cache_count = 0;
  std::fill_n(g_hotbar, dis::kMaxFavorites, 0u);
  g_hotbar_initialized = false;
  g_last_logged_magic_count = UINT32_MAX;
  g_last_logged_favorite_count = UINT32_MAX;
  g_all_favorite_count = 0;
  g_last_tamriel_position_valid = false;
  g_last_tamriel_x = g_last_tamriel_y = g_last_tamriel_z = 0.0f;
  g_native_map_requested_until_ms.store(0, std::memory_order_release);
  g_native_map_active_request.store(0, std::memory_order_release);
  g_native_map_request_started_ms.store(0, std::memory_order_release);
  g_native_map_terminal_request.store(0, std::memory_order_release);
  g_native_map_lock_misses.store(0, std::memory_order_release);
  g_native_map_last_failure_reason.store(nullptr, std::memory_order_release);
  g_native_map_last_failure_hr.store(S_OK, std::memory_order_release);
  g_native_map_center_x.store(0.0f, std::memory_order_release);
  g_native_map_center_y.store(0.0f, std::memory_order_release);
  g_native_map_center_z.store(0.0f, std::memory_order_release);
  g_native_map_center_initialized.store(false, std::memory_order_release);
  g_native_map_world_id.store(0, std::memory_order_release);
  g_native_map_player_x.store(0.0f, std::memory_order_release);
  g_native_map_player_y.store(0.0f, std::memory_order_release);
  g_native_map_player_z.store(0.0f, std::memory_order_release);
  g_native_map_player_heading.store(0.0f, std::memory_order_release);
  g_native_map_resolved_x.store(0.0f, std::memory_order_release);
  g_native_map_resolved_y.store(0.0f, std::memory_order_release);
  g_native_map_resolved_z.store(0.0f, std::memory_order_release);
  g_native_map_anchor_resolved.store(false, std::memory_order_release);
  g_native_map_view_offset_x.store(0.0f, std::memory_order_release);
  g_native_map_view_offset_y.store(0.0f, std::memory_order_release);
  g_native_map_view_offset_z.store(0.0f, std::memory_order_release);
  g_native_map_view_zoom.store(0.25f, std::memory_order_release);
  g_native_map_view_initialized.store(false, std::memory_order_release);
  g_native_map_pending_pan_x.store(0.0f, std::memory_order_release);
  g_native_map_pending_pan_y.store(0.0f, std::memory_order_release);
  g_native_map_pending_zoom.store(0.0f, std::memory_order_release);
  g_native_map_control_revision.store(0, std::memory_order_release);
  g_native_map_applied_control_revision.store(0,
                                               std::memory_order_release);
  g_map_marker_cache_valid.store(false, std::memory_order_release);
  g_map_marker_refresh_requested.store(false, std::memory_order_release);
  g_last_map_marker_refresh_ms = 0;
  AcquireSRWLockExclusive(&g_map_marker_cache_lock);
  g_map_marker_cache_count = 0;
  for (auto& marker : g_map_marker_cache) marker = {};
  ReleaseSRWLockExclusive(&g_map_marker_cache_lock);
  g_last_logged_marker_count = UINT32_MAX;
  g_first_live_sample = true;
  g_last_summary_log = 0;
  g_last_transient_log = 0;
  g_last_transient_reason = nullptr;
  g_stable_player = nullptr;
  g_stable_cell = nullptr;
  g_stable_world_samples = 0;
  g_last_runtime_gate = -1;
  g_next_update_sample.store(0, std::memory_order_release);
  g_perf_sample = {};
  g_perf_inventory_signature = {};
  g_perf_inventory_rebuild = {};
  g_perf_map_signature = {};
  g_perf_map_rebuild = {};
  g_perf_magic_signature = {};
  g_perf_magic_rebuild = {};
  g_perf_submit = {};
  g_perf_window_started_ms = 0;
  // UI3D, Inventory3DManager, and D3D resources are owned by the render
  // thread.  Publish a generation change here; the primary Present hook will
  // detach/cancel them without invoking renderer APIs from an SKSE lifecycle
  // callback.
  g_native_render_reset_revision.fetch_add(1, std::memory_order_acq_rel);
  g_custom_preview_manifest_signature = 0;
  if (g_custom_character_preview_enabled)
    dis::player_preview::ResetWorld(
        g_world_epoch.load(std::memory_order_acquire));
}

void SuspendSampling() {
  g_game_ready.store(false, std::memory_order_release);
  g_sample_not_before.store(kSamplingDisabled, std::memory_order_release);
  g_world_epoch.fetch_add(1, std::memory_order_acq_rel);
  if (!g_renderer_ready.load(std::memory_order_acquire))
    g_renderer_not_before.store(kSamplingDisabled, std::memory_order_release);
}

void ArmSampling(uint64_t delay_ms) {
  const uint64_t not_before = GetTickCount64() + delay_ms;
  g_sample_not_before.store(not_before, std::memory_order_release);
  if (!g_renderer_ready.load(std::memory_order_acquire))
    g_renderer_not_before.store(not_before, std::memory_order_release);
  g_game_ready.store(true, std::memory_order_release);
}

bool LiveWorldReadyForNativeRendering() {
  return g_running.load(std::memory_order_acquire) &&
         g_data_loaded.load(std::memory_order_acquire) &&
         g_game_ready.load(std::memory_order_acquire) &&
         g_live_snapshot_ready.load(std::memory_order_acquire) &&
         g_renderer_ready.load(std::memory_order_acquire);
}

const char* LoadingReasonName(dis::LoadingReason reason) {
  switch (reason) {
    case dis::LoadingReason::kStartup: return "startup";
    case dis::LoadingReason::kPreLoadGame: return "pre-load";
    case dis::LoadingReason::kPostLoadFailed: return "post-load-failed";
    case dis::LoadingReason::kNewGame: return "new-game";
    default: return "none";
  }
}

void SubmitLoadingState(dis::LoadingReason reason) {
  dis::Snapshot& empty = g_loading_snapshot;
  empty = {};
  empty.version = dis::kBridgeVersion;
  empty.size = sizeof(dis::Snapshot);
  empty.sequence = ++g_sequence;
  empty.world_epoch = g_world_epoch.load(std::memory_order_acquire);
  empty.loading_reason = reason;
  if (g_submit) g_submit(&empty);
  PluginLog(false, "snapshot: waiting sequence=%llu epoch=%llu reason=%s",
            static_cast<unsigned long long>(empty.sequence),
            static_cast<unsigned long long>(empty.world_epoch),
            LoadingReasonName(reason));
}

void LogTransientSampleSkip(const char* reason, void* player, void* cell) {
  const uint64_t now = GetTickCount64();
  if (reason != g_last_transient_reason || now - g_last_transient_log >= 5000) {
    PluginLog(false,
              "sample: retained last live snapshot reason=%s player=%p cell=%p "
              "stable=%u epoch=%llu",
              reason ? reason : "unknown", player, cell, g_stable_world_samples,
              static_cast<unsigned long long>(
                  g_world_epoch.load(std::memory_order_acquire)));
    g_last_transient_reason = reason;
    g_last_transient_log = now;
  }
}

bool KeepLiveOrWait(const char* reason, void* player, void* cell) {
  if (g_live_snapshot_ready.load(std::memory_order_acquire)) {
    LogTransientSampleSkip(reason, player, cell);
    return true;
  }
  // The startup waiting frame was already published by the lifecycle event.
  // Avoid sending a new identical empty frame on every PlayerCharacter update.
  if (GetTickCount64() - g_last_transient_log >= 5000) {
    PluginLog(false, "sample: world not live reason=%s player=%p cell=%p stable=%u",
              reason ? reason : "unknown", player, cell, g_stable_world_samples);
    g_last_transient_log = GetTickCount64();
  }
  return true;
}

void DiscardQueuedCommands() {
  if (!g_poll) return;
  dis::Command ignored;
  // The bridge queue is bounded to 64 entries. Draining it at every world
  // boundary prevents a tap made in the previous save from being replayed
  // against a form with the same ID after the next save becomes live.
  for (unsigned i = 0;
       i < 64 && g_poll(&ignored, static_cast<uint32_t>(sizeof(ignored)),
                        dis::kBridgeVersion);
       ++i) {
    if (ignored.type == dis::CommandType::kRequestItemPreview)
      CompleteNativePreviewFailure(ignored.form_id, ignored.world_epoch,
                                   "world-bound command discarded");
  }
}

void Sample();
void ExecuteCommand(void* player, const dis::Command& command,
                    bool deferred_retry);
bool InstallPlayerUpdateHook();
bool PrepareCharacterPreviewForEquipmentMutation();
void ServiceEquipmentSettlement(void* player);

void OnSkseMessage(SKSEMessagingInterface::Message* message) {
  if (!message) return;
  switch (message->type) {
    case kMessagePreLoadGame:
      SuspendSampling();
      g_live_snapshot_ready.store(false, std::memory_order_release);
      AcquireSRWLockExclusive(&g_sample_lock);
      ResetLiveCaches();
      std::fill_n(g_loaded_hotbar, dis::kMaxFavorites, 0u);
      g_loaded_hotbar_available = false;
      DiscardQueuedCommands();
      SubmitLoadingState(dis::LoadingReason::kPreLoadGame);
      ReleaseSRWLockExclusive(&g_sample_lock);
      PluginLog(false, "lifecycle: pre-load; sampling suspended");
      break;
    case kMessagePostLoadGame: {
      // SKSE encodes PostLoadGame success in the pointer value itself (nullptr
      // or reinterpret_cast<void*>(1)); it is not an address to dereference.
      // Dereferencing 0x1 is why every save load died inside this callback.
      const bool success = reinterpret_cast<uintptr_t>(message->data) != 0;
      SuspendSampling();
      AcquireSRWLockExclusive(&g_sample_lock);
      ResetLiveCaches();
      DiscardQueuedCommands();
      g_live_snapshot_ready.store(false, std::memory_order_release);
      ReleaseSRWLockExclusive(&g_sample_lock);
      if (success) {
        ArmSampling(1500);
      } else {
        SubmitLoadingState(dis::LoadingReason::kPostLoadFailed);
      }
      PluginLog(false, "lifecycle: post-load success=%u; sampling %s",
                success ? 1u : 0u, success ? "deferred" : "suspended");
      break;
    }
    case kMessageNewGame:
      SuspendSampling();
      AcquireSRWLockExclusive(&g_sample_lock);
      ResetLiveCaches();
      std::fill_n(g_loaded_hotbar, dis::kMaxFavorites, 0u);
      g_loaded_hotbar_available = false;
      DiscardQueuedCommands();
      g_live_snapshot_ready.store(false, std::memory_order_release);
      SubmitLoadingState(dis::LoadingReason::kNewGame);
      ReleaseSRWLockExclusive(&g_sample_lock);
      // SKSE sends NewGame after creating the new game but before the world has
      // finished loading.  Player/cell pointers can already look valid here
      // while ActorValueOwner is still being initialized.
      ArmSampling(5000);
      PluginLog(false, "lifecycle: new game; sampling deferred until world settles");
      break;
    case kMessageDataLoaded:
      // DataLoaded is dispatched by Skyrim's canonical lifecycle/game thread.
      // Earlier generic SKSE notifications may originate in plugin loading and
      // must not become the thread-affinity reference for in-world sampling.
      g_lifecycle_thread_id.store(GetCurrentThreadId(), std::memory_order_release);
      g_data_loaded.store(true, std::memory_order_release);
      // Never create Skyrim's second DXGI swapchain on the main menu. Wine/DXVK
      // accepts the creation and presents the panel, then the game dies shortly
      // afterwards. The worker attaches only after the first fully validated
      // in-world snapshot has been sampled.
      g_renderer_not_before.store(kSamplingDisabled, std::memory_order_release);
      PluginLog(false, "lifecycle: game data loaded");
      break;
    default:
      break;
  }
}

bool RegionAllows(const void* address, size_t size, bool execute = false) {
  if (!address || size == 0) return false;
  MEMORY_BASIC_INFORMATION info = {};
  if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
      (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
  const uintptr_t start = reinterpret_cast<uintptr_t>(address);
  if (start + size < start || start + size >
      reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize) return false;
  if (!execute) return true;
  const DWORD p = info.Protect & 0xff;
  return p == PAGE_EXECUTE || p == PAGE_EXECUTE_READ ||
         p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
}

bool RegionWritable(const void* address, size_t size) {
  if (!address || size == 0) return false;
  MEMORY_BASIC_INFORMATION info = {};
  if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
      (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
  const uintptr_t start = reinterpret_cast<uintptr_t>(address);
  if (start + size < start || start + size >
      reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize) return false;
  const DWORD protection = info.Protect & 0xff;
  return protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
         protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

void ReleaseNiObject(void* object) {
  if (!object || !RegionWritable(reinterpret_cast<uint8_t*>(object) + 8,
                                  sizeof(LONG)))
    return;
  const LONG remaining = InterlockedDecrement(reinterpret_cast<volatile LONG*>(
      reinterpret_cast<uint8_t*>(object) + 8));
  if (remaining != 0) return;
  void** table = nullptr;
  if (RegionAllows(object, sizeof(table)))
    std::memcpy(&table, object, sizeof(table));
  void* destroy = nullptr;
  if (table && RegionAllows(table + 1, sizeof(destroy)))
    std::memcpy(&destroy, table + 1, sizeof(destroy));
  if (RegionAllows(destroy, 1, true))
    reinterpret_cast<void(*)(void*)>(destroy)(object);
}

// A full live actor clone reaches the GPU correctly, but one device run proved
// that Skyrim faults after its root reference reaches zero.  Keep this handler
// dormant outside that exact release window.  If a native child destructor
// faults, the raw exception PC/registers and return addresses survive Android's
// otherwise backtrace-free Wine process termination and identify the real
// subobject/engine routine on the following log pull.
LONG WINAPI CharacterCloneReleaseExceptionHandler(EXCEPTION_POINTERS* info) {
  if (!g_character_clone_release_probe.load(std::memory_order_acquire) ||
      GetCurrentThreadId() !=
          g_character_clone_release_thread.load(std::memory_order_acquire) ||
      !info || !info->ExceptionRecord || !info->ContextRecord)
    return EXCEPTION_CONTINUE_SEARCH;
  if (g_character_clone_exception_reporting.test_and_set(
          std::memory_order_acq_rel))
    return EXCEPTION_CONTINUE_SEARCH;

  const auto* record = info->ExceptionRecord;
  const auto* context = info->ContextRecord;
  const uintptr_t pc = reinterpret_cast<uintptr_t>(record->ExceptionAddress);
  const uintptr_t pc_rva = g_base && pc >= g_base ? pc - g_base : 0;
#if defined(_M_X64) || defined(__x86_64__)
  PluginLog(false,
            "[CHAR_FATAL_EXCEPTION] character clone release exception "
            "code=%08lX flags=%08lX pc=%p rva=%llX clone=%p "
            "rip=%llX rsp=%llX rbp=%llX rcx=%llX rdx=%llX "
            "r8=%llX r9=%llX",
            static_cast<unsigned long>(record->ExceptionCode),
            static_cast<unsigned long>(record->ExceptionFlags),
            record->ExceptionAddress,
            static_cast<unsigned long long>(pc_rva),
            g_character_clone_release_object.load(std::memory_order_acquire),
            static_cast<unsigned long long>(context->Rip),
            static_cast<unsigned long long>(context->Rsp),
            static_cast<unsigned long long>(context->Rbp),
            static_cast<unsigned long long>(context->Rcx),
            static_cast<unsigned long long>(context->Rdx),
            static_cast<unsigned long long>(context->R8),
            static_cast<unsigned long long>(context->R9));
  if (record->NumberParameters >= 2) {
    PluginLog(false,
              "[CHAR_FATAL_EXCEPTION] access=%llu address=%p parameters=%lu",
              static_cast<unsigned long long>(record->ExceptionInformation[0]),
              reinterpret_cast<void*>(record->ExceptionInformation[1]),
              static_cast<unsigned long>(record->NumberParameters));
  }
  const uintptr_t stack_pointer = static_cast<uintptr_t>(context->Rsp);
  for (uint32_t index = 0; index < 24; ++index) {
    const void* slot = reinterpret_cast<const void*>(
        stack_pointer + static_cast<uintptr_t>(index) * sizeof(uintptr_t));
    uintptr_t value = 0;
    if (!RegionAllows(slot, sizeof(value))) break;
    std::memcpy(&value, slot, sizeof(value));
    if (!value) continue;
    const uintptr_t value_rva =
        g_base && value >= g_base ? value - g_base : 0;
    PluginLog(false,
              "[CHAR_FATAL_EXCEPTION] stack[%u]=%p rva=%llX",
              index, reinterpret_cast<void*>(value),
              static_cast<unsigned long long>(value_rva));
  }
#else
  PluginLog(false,
            "[CHAR_FATAL_EXCEPTION] character clone release exception "
            "code=%08lX pc=%p rva=%llX clone=%p",
            static_cast<unsigned long>(record->ExceptionCode),
            record->ExceptionAddress,
            static_cast<unsigned long long>(pc_rva),
            g_character_clone_release_object.load(std::memory_order_acquire));
#endif
  g_character_clone_exception_reporting.clear(std::memory_order_release);
  return EXCEPTION_CONTINUE_SEARCH;
}

template <typename T>
bool Read(const void* address, T* out) {
  if (!out || !RegionAllows(address, sizeof(T))) return false;
  std::memcpy(out, address, sizeof(T));
  return true;
}

template <typename T>
T ReadOr(const void* address, T fallback = {}) {
  T result = fallback;
  Read(address, &result);
  return result;
}

template <typename T>
bool WriteValue(void* address, const T& value) {
  if (!RegionWritable(address, sizeof(T))) return false;
  std::memcpy(address, &value, sizeof(T));
  return true;
}

// Skyrim's UI3D renderer is not a general-purpose scene entry point: its
// original 1.5.97 implementation exits at SkyrimSE+0x8D240C whenever
// UI::numDisablePauseMenu is zero. That counter is normally owned by the UI
// menu stack. Keep the game-visible value non-zero only around our exact
// off-screen render call and use interlocked arithmetic so a simultaneous
// native menu transition cannot be overwritten by restoring a stale value.
class ScopedExclusiveSrwLock {
 public:
  explicit ScopedExclusiveSrwLock(SRWLOCK* lock) : lock_(lock) {
    if (lock_) AcquireSRWLockExclusive(lock_);
  }
  ~ScopedExclusiveSrwLock() {
    if (lock_) ReleaseSRWLockExclusive(lock_);
  }
  ScopedExclusiveSrwLock(const ScopedExclusiveSrwLock&) = delete;
  ScopedExclusiveSrwLock& operator=(const ScopedExclusiveSrwLock&) = delete;

 private:
  SRWLOCK* lock_ = nullptr;
};

class ScopedAtomicMutationGate {
 public:
  explicit ScopedAtomicMutationGate(std::atomic<bool>* flag) : flag_(flag) {
    if (!flag_) {
      acquired_ = true;
      return;
    }
    bool expected = false;
    acquired_ = flag_->compare_exchange_strong(
        expected, true, std::memory_order_acq_rel, std::memory_order_acquire);
  }
  ~ScopedAtomicMutationGate() {
    if (flag_ && acquired_)
      flag_->store(false, std::memory_order_release);
  }
  ScopedAtomicMutationGate(const ScopedAtomicMutationGate&) = delete;
  ScopedAtomicMutationGate& operator=(const ScopedAtomicMutationGate&) = delete;

  explicit operator bool() const { return acquired_; }

 private:
  std::atomic<bool>* flag_ = nullptr;
  bool acquired_ = false;
};

class ScopedUi3DRenderPermission {
 public:
  ScopedUi3DRenderPermission() {
    void* ui = ReadOr<void*>(reinterpret_cast<void*>(g_base + kUIPtr),
                             nullptr);
    if (!ui || !RegionWritable(
                   reinterpret_cast<uint8_t*>(ui) +
                       kUiNumDisablePauseMenuOffset,
                   sizeof(LONG))) {
      g_ui3d_gate_failures.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    counter_ = reinterpret_cast<volatile LONG*>(
        reinterpret_cast<uint8_t*>(ui) + kUiNumDisablePauseMenuOffset);
    const LONG value = InterlockedIncrement(counter_);
    if (value <= 0 || value > 1024) {
      InterlockedDecrement(counter_);
      counter_ = nullptr;
      g_ui3d_gate_failures.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    active_ = true;
    observed_value_ = value;
    g_ui3d_gate_scopes.fetch_add(1, std::memory_order_relaxed);
  }

  ~ScopedUi3DRenderPermission() {
    if (active_ && counter_) InterlockedDecrement(counter_);
  }

  ScopedUi3DRenderPermission(const ScopedUi3DRenderPermission&) = delete;
  ScopedUi3DRenderPermission& operator=(
      const ScopedUi3DRenderPermission&) = delete;

  explicit operator bool() const { return active_; }
  LONG observed_value() const { return observed_value_; }

 private:
  volatile LONG* counter_ = nullptr;
  LONG observed_value_ = 0;
  bool active_ = false;
};

struct UiRendererCounters {
  uint32_t disable_pause = 0;
  uint32_t custom_rendering = 0;
  bool valid = false;
};

UiRendererCounters ReadUiRendererCounters() {
  UiRendererCounters result{};
  void* ui = ReadOr<void*>(reinterpret_cast<void*>(g_base + kUIPtr), nullptr);
  if (!ui || !RegionAllows(reinterpret_cast<uint8_t*>(ui) +
                               kUiNumCustomRenderingOffset,
                           sizeof(uint32_t)))
    return result;
  result.disable_pause = ReadOr<uint32_t>(
      reinterpret_cast<uint8_t*>(ui) + kUiNumDisablePauseMenuOffset, 0);
  result.custom_rendering = ReadOr<uint32_t>(
      reinterpret_cast<uint8_t*>(ui) + kUiNumCustomRenderingOffset, 0);
  result.valid = true;
  return result;
}

void* SkyrimThreadTls(DWORD index) {
  if (index == TLS_OUT_OF_INDEXES) return nullptr;
  void** slots = nullptr;
#if defined(__GNUC__) && defined(__x86_64__)
  // Match SkyrimSE.exe exactly. This is the PE loader's static-TLS vector,
  // which is a different namespace from Win32's dynamic TlsGetValue slots.
  // Both PlayerCharacter::Update and ActorValueOwner read GS:[0x58] directly.
  __asm__ __volatile__("movq %%gs:0x58, %0" : "=r"(slots) : : "memory");
#else
#error SkyrimSecondScreen requires an x86-64 compiler with direct GS access
#endif
  if (!slots) return nullptr;
  const uintptr_t base = reinterpret_cast<uintptr_t>(slots);
  if (index > (UINTPTR_MAX - base) / sizeof(void*)) return nullptr;
  void** const slot = reinterpret_cast<void**>(base + index * sizeof(void*));
  return ReadOr<void*>(slot, nullptr);
}

void CopyText(char* output, size_t output_size, const char* text) {
  if (!output || output_size == 0) return;
  output[0] = 0;
  if (!text) return;
  MEMORY_BASIC_INFORMATION info = {};
  if (!VirtualQuery(text, &info, sizeof(info)) || info.State != MEM_COMMIT ||
      (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return;
  const uintptr_t start = reinterpret_cast<uintptr_t>(text);
  const uintptr_t end = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
  const size_t readable = end > start ? static_cast<size_t>(end - start) : 0;
  const size_t limit = std::min(output_size - 1, readable);
  size_t n = 0;
  for (; n < limit; ++n) {
    const char c = text[n];
    if (c == 0) break;
    output[n] = (static_cast<unsigned char>(c) >= 0x20 || c == '\t') ? c : ' ';
  }
  output[n] = 0;
}

// Report the exact Skyrim scene objects, shader materials, and texture-set
// paths that reached the native renderer.  This deliberately inspects the
// already-resolved Ni scene rather than inventing a parallel FormID-to-file
// database.  It runs only when a new item/character scene is adopted and is
// bounded so diagnostics cannot become a per-frame traversal cost.
void LogSceneResources(const char* subsystem, uint32_t form_id, void* root) {
  if (!subsystem || !root || !RegionAllows(root, 0x110)) return;
  constexpr uint32_t kQueueCapacity = 512;
  constexpr uint32_t kGeometryLogLimit = 16;
  void* queue[kQueueCapacity] = {};
  uint32_t read_index = 0;
  uint32_t queue_count = 1;
  uint32_t geometry_count = 0;
  uint32_t geometry_logged = 0;
  uint32_t texture_paths = 0;
  queue[0] = root;

  while (read_index < queue_count && read_index < kQueueCapacity) {
    void* object = queue[read_index++];
    if (!object || !RegionAllows(object, 0x110)) continue;
    if (void* geometry = SceneVirtualPointer(object, 7)) {
      ++geometry_count;
      if (geometry_logged < kGeometryLogLimit &&
          RegionAllows(geometry, 0x150)) {
        char geometry_name[128] = {};
        CopyText(geometry_name, sizeof(geometry_name),
                 ReadOr<const char*>(
                     reinterpret_cast<uint8_t*>(geometry) + 0x10, nullptr));
        // BSGeometry has two NiProperty states at +0x120. Models commonly
        // place NiAlphaProperty first and the actual BSShaderProperty second;
        // treating slot zero as the shader made valid textured geometry look
        // unmaterialized in diagnostics.
        void* properties[2] = {
            ReadOr<void*>(reinterpret_cast<uint8_t*>(geometry) + 0x120,
                          nullptr),
            ReadOr<void*>(reinterpret_cast<uint8_t*>(geometry) + 0x128,
                          nullptr)};
        char property_types[2][96] = {};
        void* shader_property = nullptr;
        for (uint32_t property_index = 0; property_index < 2;
             ++property_index) {
          void* property_rtti =
              properties[property_index]
                  ? SceneVirtualPointer(properties[property_index], 2)
                  : nullptr;
          if (property_rtti && RegionAllows(property_rtti, 0x10)) {
            CopyText(property_types[property_index],
                     sizeof(property_types[property_index]),
                     ReadOr<const char*>(property_rtti, nullptr));
          }
          if (!shader_property &&
              std::strstr(property_types[property_index],
                          "ShaderProperty") != nullptr)
            shader_property = properties[property_index];
        }
        const char* shader_type = "<none>";
        for (uint32_t property_index = 0; property_index < 2;
             ++property_index) {
          if (properties[property_index] == shader_property &&
              property_types[property_index][0]) {
            shader_type = property_types[property_index];
            break;
          }
        }
        const bool lighting_property =
            std::strstr(shader_type, "LightingShaderProperty") != nullptr;
        const bool effect_property =
            std::strstr(shader_type, "EffectShaderProperty") != nullptr;
        void* material = shader_property && RegionAllows(shader_property, 0x80)
                             ? ReadOr<void*>(
                                   reinterpret_cast<uint8_t*>(shader_property) +
                                       0x78,
                                   nullptr)
                             : nullptr;
        void* texture_set = lighting_property && material &&
                                    RegionAllows(material, 0x80)
                                ? ReadOr<void*>(
                                      reinterpret_cast<uint8_t*>(material) +
                                          0x78,
                                      nullptr)
                                : nullptr;
        void* effect_source = effect_property && material &&
                                      RegionAllows(material, 0x80)
                                  ? ReadOr<void*>(
                                        reinterpret_cast<uint8_t*>(material) +
                                            0x58,
                                        nullptr)
                                  : nullptr;
        void* effect_greyscale = effect_property && material &&
                                         RegionAllows(material, 0x80)
                                     ? ReadOr<void*>(
                                           reinterpret_cast<uint8_t*>(material) +
                                               0x60,
                                           nullptr)
                                     : nullptr;
        PluginLog(false,
                  "[RESOURCE] %s form=%08X geometry=%u node=\"%s\" "
                  "properties=%s|%s shader_type=%s shader=%p material=%p "
                  "textures=%p effect_source=%p effect_greyscale=%p skin=%p "
                  "renderer_data=%p",
                  subsystem, form_id, geometry_count,
                  geometry_name[0] ? geometry_name : "<unnamed>",
                  property_types[0][0] ? property_types[0] : "<none>",
                  property_types[1][0] ? property_types[1] : "<none>",
                  shader_type,
                  shader_property, material, texture_set, effect_source,
                  effect_greyscale,
                  ReadOr<void*>(reinterpret_cast<uint8_t*>(geometry) + 0x130,
                                nullptr),
                  ReadOr<void*>(reinterpret_cast<uint8_t*>(geometry) + 0x138,
                                nullptr));

        // BSTextureSet::GetTexturePath is virtual slot 0x25 in Skyrim 1.5.97.
        // Only call it after validating both the set and executable slot.  The
        // nine indices are Skyrim's complete diffuse/normal/glow/etc. set.
        if (texture_set && RegionAllows(texture_set, sizeof(void*))) {
          void** table = ReadOr<void**>(texture_set, nullptr);
          void* raw_get_path =
              table ? ReadOr<void*>(table + 0x25, nullptr) : nullptr;
          if (RegionAllows(raw_get_path, 1, true)) {
            using GetTexturePathFn = const char*(*)(void*, uint32_t);
            auto get_texture_path =
                reinterpret_cast<GetTexturePathFn>(raw_get_path);
            for (uint32_t texture_index = 0; texture_index < 9;
                 ++texture_index) {
              char path[260] = {};
              CopyText(path, sizeof(path),
                       get_texture_path(texture_set, texture_index));
              if (!path[0]) continue;
              ++texture_paths;
              PluginLog(false,
                        "[RESOURCE] %s form=%08X geometry=%u "
                        "texture[%u]=\"%s\"",
                        subsystem, form_id, geometry_count, texture_index,
                        path);
            }
          }
        }
        if (effect_property && material && RegionAllows(material, 0x80)) {
          const uintptr_t path_offsets[] = {0x70, 0x78};
          const char* path_labels[] = {"effect-source", "effect-greyscale"};
          for (uint32_t path_index = 0; path_index < 2; ++path_index) {
            char path[260] = {};
            CopyText(path, sizeof(path),
                     ReadOr<const char*>(
                         reinterpret_cast<uint8_t*>(material) +
                             path_offsets[path_index],
                         nullptr));
            if (!path[0]) continue;
            ++texture_paths;
            PluginLog(false,
                      "[RESOURCE] %s form=%08X geometry=%u %s=\"%s\"",
                      subsystem, form_id, geometry_count,
                      path_labels[path_index], path);
          }
        }
        ++geometry_logged;
      }
    }

    if (void* node = SceneVirtualPointer(object, 3)) {
      void** children = ReadOr<void**>(
          reinterpret_cast<uint8_t*>(node) + 0x118, nullptr);
      uint16_t capacity = ReadOr<uint16_t>(
          reinterpret_cast<uint8_t*>(node) + 0x120, 0);
      capacity = std::min<uint16_t>(capacity, 512);
      for (uint16_t i = 0;
           children && i < capacity && queue_count < kQueueCapacity; ++i) {
        void* child = ReadOr<void*>(children + i, nullptr);
        if (!child || !RegionAllows(child, sizeof(void*))) continue;
        bool duplicate = false;
        for (uint32_t prior = 0; prior < queue_count; ++prior) {
          if (queue[prior] == child) {
            duplicate = true;
            break;
          }
        }
        if (!duplicate) queue[queue_count++] = child;
      }
    }
  }
  PluginLog(false,
            "[RESOURCE] %s form=%08X summary objects=%u geometry=%u "
            "logged=%u texture_paths=%u truncated=%u",
            subsystem, form_id, read_index, geometry_count, geometry_logged,
            texture_paths, queue_count >= kQueueCapacity ? 1u : 0u);
}

void* Player() {
  return ReadOr<void*>(reinterpret_cast<void*>(g_base + kPlayerPtr), nullptr);
}

bool ActorValueRuntimeReady(void* player) {
  int blocked = 0;
  void* loaded_state = nullptr;
  void* root_node = nullptr;
  void* actor_values = nullptr;
  void* tls_data = nullptr;
  void** table = nullptr;
  DWORD tls_index = TLS_OUT_OF_INDEXES;

  if (!player || !RegionAllows(player, 0xBE0)) {
    blocked = 1;
  } else {
    loaded_state = ReadOr<void*>(reinterpret_cast<uint8_t*>(player) + 0x68, nullptr);
    if (!loaded_state || !RegionAllows(loaded_state, 0x70)) {
      blocked = 2;
    } else {
      root_node = ReadOr<void*>(reinterpret_cast<uint8_t*>(loaded_state) + 0x68, nullptr);
      if (!root_node || !RegionAllows(root_node, sizeof(void*))) blocked = 3;
    }
  }

  if (!blocked) {
    actor_values = ReadOr<void*>(reinterpret_cast<void*>(g_base + kActorValueListPtr), nullptr);
    if (!actor_values) {
      blocked = 4;
    } else {
      constexpr UInt32 required[] = {24, 25, 26, 32};
      for (UInt32 id : required) {
        const void* slot = reinterpret_cast<uint8_t*>(actor_values) + 8 + id * sizeof(void*);
        if (!RegionAllows(slot, sizeof(void*)) ||
            !ReadOr<void*>(slot, nullptr)) {
          blocked = 5;
          break;
        }
      }
    }
  }

  if (!blocked) {
    tls_index = ReadOr<DWORD>(reinterpret_cast<void*>(g_base + kTlsIndexPtr),
                              TLS_OUT_OF_INDEXES);
    if (tls_index == TLS_OUT_OF_INDEXES) {
      blocked = 6;
    } else {
      tls_data = SkyrimThreadTls(tls_index);
      if (!tls_data || !RegionAllows(tls_data, 0x76C)) blocked = 7;
    }
  }

  if (!blocked) {
    void* owner = reinterpret_cast<uint8_t*>(player) + 0xB0;
    table = ReadOr<void**>(owner, nullptr);
    const void* expected_table = reinterpret_cast<void*>(g_base + kPlayerActorValueVtable);
    const void* current = table ? ReadOr<void*>(table + 1, nullptr) : nullptr;
    const void* maximum = table ? ReadOr<void*>(table + 2, nullptr) : nullptr;
    if (table != expected_table ||
        current != reinterpret_cast<void*>(g_base + kActorValueGetCurrent) ||
        maximum != reinterpret_cast<void*>(g_base + kActorValueGetMaximum)) {
      blocked = 8;
    }
  }

  if (blocked != g_last_runtime_gate) {
    g_last_runtime_gate = blocked;
    if (blocked) {
      PluginLog(false,
                "runtime gate: blocked=%d thread=%lu lifecycle_thread=%lu player=%p "
                "loaded=%p node=%p avlist=%p tls_index=%lu tls=%p av_vtable=%p",
                blocked, static_cast<unsigned long>(GetCurrentThreadId()),
                static_cast<unsigned long>(g_lifecycle_thread_id), player, loaded_state,
                root_node, actor_values, static_cast<unsigned long>(tls_index), tls_data, table);
    } else {
      PluginLog(false,
                "runtime gate: ready thread=%lu lifecycle_thread=%lu player=%p "
                "loaded=%p node=%p avlist=%p tls_index=%lu tls=%p av_vtable=%p",
                static_cast<unsigned long>(GetCurrentThreadId()),
                static_cast<unsigned long>(g_lifecycle_thread_id), player, loaded_state,
                root_node, actor_values, static_cast<unsigned long>(tls_index), tls_data, table);
    }
  }
  return blocked == 0;
}

void PlayerUpdateHook(void* player, float delta_seconds) {
  const PlayerUpdateFn original =
      g_player_update_original.load(std::memory_order_acquire);
  if (!original || original == &PlayerUpdateHook) {
    // This cannot occur after a validated installation. Refusing recursion is
    // safer than entering a corrupt vtable chain.
    return;
  }

  // Preserve Skyrim's update completely. Sampling happens only after the real
  // PlayerCharacter update has finished on the same TLS-owning game thread.
  original(player, delta_seconds);

  if (!g_running.load(std::memory_order_acquire) ||
      !g_data_loaded.load(std::memory_order_acquire) ||
      !g_game_ready.load(std::memory_order_acquire)) return;

  const uint64_t world_epoch = g_world_epoch.load(std::memory_order_acquire);
  const uint64_t now = GetTickCount64();
  if (now < g_sample_not_before.load(std::memory_order_acquire)) return;

  // Gameplay actions use a dedicated bridge lane and must be observed at the
  // player's frame cadence. Previously all commands waited behind the 100 ms
  // snapshot sampler; a preview transaction could add another retry interval
  // and make an equip tap feel visibly delayed.
  uint64_t next = g_next_update_sample.load(std::memory_order_acquire);
  bool sample_due = false;
  if (now >= next) {
    sample_due = g_next_update_sample.compare_exchange_strong(
        next, now + 100, std::memory_order_acq_rel,
        std::memory_order_acquire);
  }
  const bool live_snapshot =
      g_live_snapshot_ready.load(std::memory_order_acquire);
  const bool command_pending =
      live_snapshot &&
      (g_deferred_equipment_command_valid ||
       (g_has_pending_command && g_has_pending_command() != 0));
  const bool service_commands =
      live_snapshot &&
      (command_pending ||
       g_equipment_settlement_active.load(std::memory_order_acquire));
  if (!sample_due && !service_commands) return;

  // The vtable belongs to PlayerCharacter, but retain an exact identity check
  // so a damaged or unexpectedly shared table can never sample another actor.
  if (!player || player != Player() || g_sampling_on_player_update) return;
  g_sampling_on_player_update = true;

  AcquireSRWLockExclusive(&g_sample_lock);
  const uint64_t now_locked = GetTickCount64();
  if (g_running.load(std::memory_order_acquire) &&
      g_data_loaded.load(std::memory_order_acquire) &&
      g_game_ready.load(std::memory_order_acquire) && player == Player()) {
    if (world_epoch != g_world_epoch.load(std::memory_order_acquire) ||
        now_locked < g_sample_not_before.load(std::memory_order_acquire)) {
      ReleaseSRWLockExclusive(&g_sample_lock);
      g_sampling_on_player_update = false;
      return;
    }
    if (sample_due &&
        g_first_player_update.exchange(false, std::memory_order_acq_rel)) {
      const DWORD tls_index = ReadOr<DWORD>(
          reinterpret_cast<void*>(g_base + kTlsIndexPtr), TLS_OUT_OF_INDEXES);
      void* tls = SkyrimThreadTls(tls_index);
      PluginLog(false,
                "player update: first sample callback thread=%lu lifecycle_thread=%lu "
                "tls_index=%lu tls=%p",
                static_cast<unsigned long>(GetCurrentThreadId()),
                static_cast<unsigned long>(
                    g_lifecycle_thread_id.load(std::memory_order_acquire)),
                static_cast<unsigned long>(tls_index), tls);
    }
    try {
      if (g_equipment_settlement_active.load(std::memory_order_acquire))
        ServiceEquipmentSettlement(player);
      // Do not begin another gameplay mutation while Skyrim is still
      // confirming the previous actor-graph transition. Preview-only work
      // remains queued in the bridge and is consumed after the same barrier.
      if (!g_equipment_settlement_active.load(std::memory_order_acquire) &&
          command_pending) {
        dis::Command command = {};
        if (g_deferred_equipment_command_valid) {
          command = g_deferred_equipment_command;
          g_deferred_equipment_command = {};
          g_deferred_equipment_command_valid = false;
          ExecuteCommand(player, command, true);
        } else if (g_poll &&
                   g_poll(&command, static_cast<uint32_t>(sizeof(command)),
                          dis::kBridgeVersion)) {
          // Poll one command per player frame. The proxy always serves the
          // user-action lane first; background preview commands therefore
          // cannot delay a later tap and are naturally rate-limited here.
          ExecuteCommand(player, command, false);
        }
      }
      if (sample_due) Sample();
    } catch (...) {
      PluginLog(false,
                "player update: command/sample threw; frame skipped safely");
    }
  }
  ReleaseSRWLockExclusive(&g_sample_lock);
  g_sampling_on_player_update = false;
}

bool InstallPlayerUpdateHook() {
  if (!g_base) return false;
  void** const slot = reinterpret_cast<void**>(
      g_base + kPlayerVtable + kPlayerUpdateSlot * sizeof(void*));
  if (!RegionAllows(slot, sizeof(void*))) {
    PluginLog(false, "player update hook failed: vtable slot is unreadable (%p)", slot);
    return false;
  }

  void* const expected = reinterpret_cast<void*>(g_base + kPlayerUpdate);
  void* const current = ReadOr<void*>(slot, nullptr);
  void* const replacement = reinterpret_cast<void*>(&PlayerUpdateHook);
  if (current == replacement) {
    const bool valid = g_player_update_original.load(std::memory_order_acquire) != nullptr;
    PluginLog(false, "player update hook already present; original_valid=%u",
              valid ? 1u : 0u);
    return valid;
  }
  if (current != expected || !RegionAllows(current, 1, true)) {
    PluginLog(false,
              "player update hook failed: runtime slot mismatch slot=%p current=%p expected=%p",
              slot, current, expected);
    return false;
  }

  DWORD old_protect = 0;
  if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old_protect)) {
    PluginLog(false, "player update hook failed: VirtualProtect error=%lu",
              static_cast<unsigned long>(GetLastError()));
    return false;
  }

  // Publish the original before the aligned pointer swap. If the game calls
  // this slot immediately on another thread, the hook can always forward.
  g_player_update_original.store(reinterpret_cast<PlayerUpdateFn>(current),
                                 std::memory_order_release);
  void* const previous = InterlockedCompareExchangePointer(
      reinterpret_cast<void* volatile*>(slot), replacement, current);
  if (previous != current) {
    g_player_update_original.store(nullptr, std::memory_order_release);
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(void*), old_protect, &ignored);
    PluginLog(false,
              "player update hook failed: vtable changed concurrently previous=%p expected=%p",
              previous, current);
    return false;
  }

  // From this point the game can enter our function, so the module must remain
  // loaded even if restoring the data page protection unexpectedly fails.
  g_player_update_hooked.store(true, std::memory_order_release);
  DWORD ignored = 0;
  const BOOL restored = VirtualProtect(slot, sizeof(void*), old_protect, &ignored);
  const DWORD restore_error = restored ? ERROR_SUCCESS : GetLastError();
  FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
  if (!restored) {
    PluginLog(false,
              "player update hook warning: installed but page protection restore failed error=%lu",
              static_cast<unsigned long>(restore_error));
  }

  PluginLog(false, "player update hook installed: vtable=%p slot=0x%llX original=%p hook=%p",
            reinterpret_cast<void*>(g_base + kPlayerVtable),
            static_cast<unsigned long long>(kPlayerUpdateSlot),
            current, replacement);
  return true;
}

float ActorValue(void* player, UInt32 id, int method) {
  if (!player) return 0.0f;
  void* owner = reinterpret_cast<uint8_t*>(player) + 0xB0;
  void** table = ReadOr<void**>(owner, nullptr);
  if (!table) return 0.0f;
  void* raw = ReadOr<void*>(table + method, nullptr);
  if (!RegionAllows(raw, 1, true)) return 0.0f;
  using Fn = float(*)(void*, UInt32);
  float value = reinterpret_cast<Fn>(raw)(owner, id);
  return std::isfinite(value) ? value : 0.0f;
}

const char* FormName(void* form) {
  if (!form) return nullptr;
  using CastFn = void*(*)(void*, int32_t, const void*, const void*, int32_t);
  auto cast = reinterpret_cast<CastFn>(g_base + kDynamicCast);
  if (!RegionAllows(reinterpret_cast<void*>(cast), 1, true)) return nullptr;
  void* full_name = cast(form, 0, reinterpret_cast<void*>(g_base + kRttiTESForm),
                         reinterpret_cast<void*>(g_base + kRttiTESFullName), 0);
  // TESFullName is a BaseFormComponent subobject; its BSFixedString is at +8.
  return full_name
             ? ReadOr<const char*>(reinterpret_cast<uint8_t*>(full_name) + 8, nullptr)
             : nullptr;
}

const char* FormEditorId(void* form) {
  if (!form || !RegionAllows(form, sizeof(void*))) return nullptr;
  void** table = ReadOr<void**>(form, nullptr);
  void* raw = table ? ReadOr<void*>(table + 0x32, nullptr) : nullptr;
  if (!RegionAllows(raw, 1, true)) return nullptr;
  using Fn = const char*(*)(void*);
  return reinterpret_cast<Fn>(raw)(form);
}

void* AsBoundObject(void* form) {
  if (!form) return nullptr;
  using CastFn = void*(*)(void*, int32_t, const void*, const void*, int32_t);
  auto cast = reinterpret_cast<CastFn>(g_base + kDynamicCast);
  if (!RegionAllows(reinterpret_cast<void*>(cast), 1, true)) return nullptr;
  return cast(form, 0, reinterpret_cast<void*>(g_base + kRttiTESForm),
              reinterpret_cast<void*>(g_base + kRttiTESBoundObject), 0);
}

void* AsContainer(void* form) {
  if (!form) return nullptr;
  using CastFn = void*(*)(void*, int32_t, const void*, const void*, int32_t);
  auto cast = reinterpret_cast<CastFn>(g_base + kDynamicCast);
  if (!RegionAllows(reinterpret_cast<void*>(cast), 1, true)) return nullptr;
  return cast(form, 0, reinterpret_cast<void*>(g_base + kRttiTESForm),
              reinterpret_cast<void*>(g_base + kRttiTESContainer), 0);
}

uint32_t FormId(void* form) {
  return form ? ReadOr<uint32_t>(reinterpret_cast<uint8_t*>(form) + 0x14, 0) : 0;
}

uint8_t FormType(void* form) {
  return form ? ReadOr<uint8_t>(reinterpret_cast<uint8_t*>(form) + 0x1A, 0) : 0;
}

bool IsNativeMagicPreviewForm(void* form) {
  const uint8_t type = FormType(form);
  return type == 0x16 || type == 0x77;  // SpellItem or TESShout
}

enum class NativeCaptureStage : uint8_t {
  kIdle,
  kLoading,
  kCapturing,
  kUnloading,
};

struct RenderTargetDataRaw {
  ID3D11Texture2D* texture = nullptr;
  ID3D11Texture2D* texture_copy = nullptr;
  ID3D11RenderTargetView* render_view = nullptr;
  ID3D11ShaderResourceView* shader_view = nullptr;
  ID3D11ShaderResourceView* shader_copy = nullptr;
  ID3D11UnorderedAccessView* unordered_view = nullptr;
};
static_assert(sizeof(RenderTargetDataRaw) == 0x30,
              "Skyrim render-target layout changed");

struct DepthStencilDataRaw {
  ID3D11Texture2D* texture = nullptr;
  ID3D11DepthStencilView* writable_views[8] = {};
  ID3D11DepthStencilView* read_only_views[8] = {};
  ID3D11ShaderResourceView* depth_shader_view = nullptr;
  ID3D11ShaderResourceView* stencil_shader_view = nullptr;
};
static_assert(sizeof(DepthStencilDataRaw) == 0x98,
              "Skyrim depth-target layout changed");

enum class DeferredUi3DTargetOwner : uint8_t {
  kNone,
  kItem,
  kCharacter,
  kMap,
  kNativeInventory,
};

// Skyrim's UI3D accumulator records actor/skinning work during PostDisplay,
// but BSGraphics does not consume all of that work until the render phase
// reaches Present. Keep only the private framebuffer/output binding alive
// across that short interval. Camera, lighting, shaders, and every other
// piece of shared state are restored synchronously by RenderCharacterOffscreen
// so the gameplay camera can never inherit the portrait camera.
struct DeferredUi3DTargetLease {
  bool active = false;
  DeferredUi3DTargetOwner owner = DeferredUi3DTargetOwner::kNone;
  ID3D11Device* origin_device = nullptr;
  RenderTargetDataRaw* framebuffer = nullptr;
  RenderTargetDataRaw saved_framebuffer{};
  RenderTargetDataRaw expected_framebuffer{};
  uint32_t* framebuffer_width = nullptr;
  uint32_t* framebuffer_height = nullptr;
  uint32_t* depth_stencil_width = nullptr;
  uint32_t* depth_stencil_height = nullptr;
  uint32_t saved_framebuffer_width = 0;
  uint32_t saved_framebuffer_height = 0;
  uint32_t saved_depth_stencil_width = 0;
  uint32_t saved_depth_stencil_height = 0;
  uint32_t expected_framebuffer_width = 0;
  uint32_t expected_framebuffer_height = 0;
  uint32_t expected_depth_stencil_width = 0;
  uint32_t expected_depth_stencil_height = 0;
  ID3D11RenderTargetView* old_views[
      D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
  ID3D11DepthStencilView* old_depth = nullptr;
  // Exact origin-context state left installed by the private UI3D pass. These
  // retained pointers are the pipeline ownership token used at Present; a
  // changed binding is never overwritten with stale pre-pass state.
  ID3D11RenderTargetView* expected_views[
      D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
  ID3D11DepthStencilView* expected_depth = nullptr;
  UINT viewport_count = 0;
  D3D11_VIEWPORT old_viewports[
      D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
  UINT expected_viewport_count = 0;
  D3D11_VIEWPORT expected_viewports[
      D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
  UINT scissor_count = 0;
  D3D11_RECT old_scissors[
      D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
  UINT expected_scissor_count = 0;
  D3D11_RECT expected_scissors[
      D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
  ID3D11Query* pipeline_query = nullptr;
  ID3D11Query* occlusion_query = nullptr;
  // Skinned UI3D shading is drained during Present, just like its framebuffer
  // work. Keep Skyrim's real Inventory light set alive for that same bounded
  // lease while restoring the shared camera synchronously.
  void* light_manager = nullptr;
  int32_t* current_light_scheme = nullptr;
  int32_t saved_light_scheme = -1;
  int32_t active_light_scheme = -1;
  int32_t dispatch_light_scheme = -1;
  bool light_override_active = false;
  uint64_t trace_sequence = 0;
  uint64_t armed_ms = 0;
};

DeferredUi3DTargetLease g_deferred_ui3d_target;

using Ui3DCullingProcessFn = void(*)(void*, const void*, void*, void*);
using Ui3DCullingProcess1Fn = void(*)(void*, void*, uint32_t);
using Ui3DCullingAppendFn = void(*)(void*, void*, uint32_t);
using Ui3DCullingBoundTestFn = bool(*)(void*, const void*);
using Ui3DAccumulatorSubmitFn = void(*)(void*, uint32_t);
using Ui3DNodeOnVisibleFn = void(*)(void*, void*, uint32_t);
std::atomic<void*> g_ui3d_culling_original{nullptr};
std::atomic<void*> g_ui3d_culling_process1_original{nullptr};
std::atomic<void*> g_ui3d_culling_append_original{nullptr};
std::atomic<void*> g_ui3d_culling_bound_test_original{nullptr};
std::atomic<void*> g_ui3d_accumulator_pass_original{nullptr};
std::atomic<void*> g_ui3d_accumulator_submit_original{nullptr};
std::atomic<void*> g_ui3d_node_on_visible_original{nullptr};
std::atomic<bool> g_ui3d_trace_hooks_installed{false};
thread_local void* g_ui3d_current_cull_object = nullptr;
// UI3DSceneManager ultimately dispatches through a process-global
// BSCullingProcess, not the pointer stored in the scene manager instance.
// Mark only our synchronous private render call so the culling hook can adjust
// the exact live process Skyrim invokes and immediately restore it afterwards.
thread_local uint32_t g_private_ui3d_render_depth = 0;
thread_local DeferredUi3DTargetOwner g_private_ui3d_render_owner =
    DeferredUi3DTargetOwner::kNone;
thread_local uint32_t g_private_ui3d_target_width = 0;
thread_local uint32_t g_private_ui3d_target_height = 0;
thread_local ID3D11RenderTargetView* g_private_ui3d_target_view = nullptr;
thread_local ID3D11DepthStencilView* g_private_ui3d_depth_view = nullptr;

struct Ui3DFrameTrace {
  bool active = false;
  DeferredUi3DTargetOwner owner = DeferredUi3DTargetOwner::kNone;
  uint64_t sequence = 0;
  void* manager = nullptr;
  void* scene = nullptr;
  ID3D11RenderTargetView* expected_view = nullptr;
  uint32_t culling_calls = 0;
  uint32_t scene_nodes_traversed = 0;
  uint32_t geometry_nodes_encountered = 0;
  uint32_t bound_tests = 0;
  uint32_t bound_passes = 0;
  uint32_t bound_rejections = 0;
  uint32_t zero_bound_rejections = 0;
  uint32_t geometry_appends = 0;
  uint32_t tree_logs = 0;
  uint32_t accumulator_pass_calls = 0;
  uint32_t accumulator_calls = 0;
  uint32_t on_visible_calls = 0;
};

Ui3DFrameTrace g_ui3d_frame_trace;

const char* DeferredUi3DTargetOwnerName(DeferredUi3DTargetOwner owner) {
  switch (owner) {
    case DeferredUi3DTargetOwner::kItem:
      return "ITEM_RENDER";
    case DeferredUi3DTargetOwner::kCharacter:
      return "CHAR_PREVIEW";
    case DeferredUi3DTargetOwner::kMap:
      return "MAP_RENDER";
    case DeferredUi3DTargetOwner::kNativeInventory:
      return "NATIVE_INVENTORY";
    default:
      return "NONE";
  }
}

bool CharacterSceneSubmissionQuiescent() {
  // Must be called while g_native_render_lock is held. This only describes
  // ownership of Skyrim's process-global framebuffer/OM/RS state; it does not
  // prove that the GPU has finished consuming the submitted actor graph.
  return !g_deferred_ui3d_target.active;
}

bool CharacterReadbackQuiescent() {
  return !g_character_render.readback_pending &&
         !g_character_render.readback_copy_issued;
}

bool CharacterCloneLifecycleQuiescent() {
  // The readback event is inserted after the actor draw and both texture
  // copies. Only its completion proves that no queued renderer work can still
  // reference the shallow NiAVObject clone or its shared skin/controller data.
  return CharacterSceneSubmissionQuiescent() &&
         CharacterReadbackQuiescent();
}

void* FindOpenInventoryMenu() {
  void* ui = ReadOr<void*>(reinterpret_cast<void*>(g_base + kUIPtr), nullptr);
  if (!ui || !RegionAllows(ui, 0x1C8)) return nullptr;
  // UI::menuStack is BSTArray<GPtr<IMenu>> at +0x110. The installed
  // InventoryMenu PostDisplay hook uses this same canonical primary vtable,
  // so an exact stack match is an earlier authoritative ownership signal than
  // waiting for that menu's first render callback.
  auto* stack = reinterpret_cast<uint8_t*>(ui) + 0x110;
  void** menus = ReadOr<void**>(stack, nullptr);
  const uint32_t count = ReadOr<uint32_t>(stack + 0x10, 0);
  if (!menus || count == 0 || count > 64 ||
      !RegionAllows(menus, static_cast<size_t>(count) * sizeof(void*)))
    return nullptr;
  void* const inventory_vtable =
      reinterpret_cast<void*>(g_base + kInventoryMenuVtable);
  for (uint32_t index = 0; index < count; ++index) {
    void* menu = ReadOr<void*>(menus + index, nullptr);
    if (menu && RegionAllows(menu, sizeof(void*)) &&
        ReadOr<void*>(menu, nullptr) == inventory_vtable)
      return menu;
  }
  return nullptr;
}

bool NativeInventoryMenuRecentlyActive(uint64_t now) {
  constexpr uint64_t kNativeInventoryQuarantineMs = 500;
  const uint64_t last =
      g_native_inventory_last_display_ms.load(std::memory_order_acquire);
  return last != 0 && now >= last && now - last < kNativeInventoryQuarantineMs;
}

bool NativeInventoryMenuOwnsUi3D(uint64_t now) {
  return FindOpenInventoryMenu() != nullptr ||
         NativeInventoryMenuRecentlyActive(now);
}

class ScopedPrivateUi3DRender {
 public:
  ScopedPrivateUi3DRender(DeferredUi3DTargetOwner owner, uint32_t width,
                          uint32_t height, ID3D11RenderTargetView* view,
                          ID3D11DepthStencilView* depth_view)
      : previous_owner_(g_private_ui3d_render_owner),
        previous_width_(g_private_ui3d_target_width),
        previous_height_(g_private_ui3d_target_height),
        previous_view_(g_private_ui3d_target_view),
        previous_depth_view_(g_private_ui3d_depth_view) {
    ++g_private_ui3d_render_depth;
    g_private_ui3d_render_owner = owner;
    g_private_ui3d_target_width = width;
    g_private_ui3d_target_height = height;
    g_private_ui3d_target_view = view;
    g_private_ui3d_depth_view = depth_view;
  }

  ~ScopedPrivateUi3DRender() {
    if (g_private_ui3d_render_depth) --g_private_ui3d_render_depth;
    g_private_ui3d_render_owner = g_private_ui3d_render_depth
                                      ? previous_owner_
                                      : DeferredUi3DTargetOwner::kNone;
    g_private_ui3d_target_width =
        g_private_ui3d_render_depth ? previous_width_ : 0;
    g_private_ui3d_target_height =
        g_private_ui3d_render_depth ? previous_height_ : 0;
    g_private_ui3d_target_view =
        g_private_ui3d_render_depth ? previous_view_ : nullptr;
    g_private_ui3d_depth_view =
        g_private_ui3d_render_depth ? previous_depth_view_ : nullptr;
  }

  ScopedPrivateUi3DRender(const ScopedPrivateUi3DRender&) = delete;
  ScopedPrivateUi3DRender& operator=(const ScopedPrivateUi3DRender&) = delete;

 private:
  DeferredUi3DTargetOwner previous_owner_ = DeferredUi3DTargetOwner::kNone;
  uint32_t previous_width_ = 0;
  uint32_t previous_height_ = 0;
  ID3D11RenderTargetView* previous_view_ = nullptr;
  ID3D11DepthStencilView* previous_depth_view_ = nullptr;
};

const char* Ui3DObjectName(void* object) {
  const char* name = object
                         ? ReadOr<const char*>(
                               reinterpret_cast<uint8_t*>(object) + 0x10,
                               nullptr)
                         : nullptr;
  return name && RegionAllows(name, 1) ? name : "<unnamed>";
}

void LogUi3DObjectState(const char* prefix, void* object, uint32_t ordinal) {
  if (!object || !RegionAllows(object, 0x110)) return;
  SceneTransform local = {};
  SceneTransform world = {};
  float bound[4] = {};
  Read(reinterpret_cast<uint8_t*>(object) + 0x48, &local);
  Read(reinterpret_cast<uint8_t*>(object) + 0x7C, &world);
  Read(reinterpret_cast<uint8_t*>(object) + 0xE4, &bound);
  const uint32_t flags =
      ReadOr<uint32_t>(reinterpret_cast<uint8_t*>(object) + 0xF4, 0);
  void* parent = ReadOr<void*>(reinterpret_cast<uint8_t*>(object) + 0x30,
                              nullptr);
  void** object_vtable = ReadOr<void**>(object, nullptr);
  void* on_visible = object_vtable
                         ? ReadOr<void*>(object_vtable + 0x34, nullptr)
                         : nullptr;
  void* update_downward = object_vtable
                              ? ReadOr<void*>(object_vtable + 0x2C, nullptr)
                              : nullptr;
  void* controllers = ReadOr<void*>(
      reinterpret_cast<uint8_t*>(object) + 0x18, nullptr);
  const uint32_t last_updated_frame = ReadOr<uint32_t>(
      reinterpret_cast<uint8_t*>(object) + 0x104, 0);
  void* geometry = SceneVirtualPointer(object, 7);
  void* node = SceneVirtualPointer(object, 3);
  void** child_data = node
                          ? ReadOr<void**>(
                                reinterpret_cast<uint8_t*>(node) + 0x118,
                                nullptr)
                          : nullptr;
  const uint16_t child_capacity =
      node ? ReadOr<uint16_t>(reinterpret_cast<uint8_t*>(node) + 0x120, 0)
           : 0;
  const uint16_t child_free_index =
      node ? ReadOr<uint16_t>(reinterpret_cast<uint8_t*>(node) + 0x122, 0)
           : 0;
  const uint16_t child_size =
      node ? ReadOr<uint16_t>(reinterpret_cast<uint8_t*>(node) + 0x124, 0)
           : 0;
  void* first_child = child_data && child_capacity
                          ? ReadOr<void*>(child_data, nullptr)
                          : nullptr;
  PluginLog(false,
            "%s seq=%llu owner=%s index=%u object=%p name=\"%.72s\" "
            "parent=%p node=%u geometry=%u child_data=%p capacity=%u "
            "free_index=%u size=%u first_child=%p vtable=%p "
            "on_visible=%p update_downward=%p controllers=%p "
            "last_frame=%u flags=%08X "
            "hidden=%u always_draw=%u fixed_bound=%u",
            prefix,
            static_cast<unsigned long long>(g_ui3d_frame_trace.sequence),
            DeferredUi3DTargetOwnerName(g_ui3d_frame_trace.owner), ordinal,
            object, Ui3DObjectName(object), parent, node ? 1u : 0u,
            geometry ? 1u : 0u, child_data,
            static_cast<unsigned>(child_capacity),
            static_cast<unsigned>(child_free_index),
            static_cast<unsigned>(child_size), first_child, object_vtable,
            on_visible, update_downward, controllers, last_updated_frame,
            flags,
            (flags & 1u) ? 1u : 0u, (flags & (1u << 11)) ? 1u : 0u,
            (flags & (1u << 13)) ? 1u : 0u);
  PluginLog(false,
            "%s seq=%llu index=%u local=(%.2f,%.2f,%.2f s=%.4f) "
            "world=(%.2f,%.2f,%.2f s=%.4f) "
            "bound=(%.2f,%.2f,%.2f r=%.3f) finite=%u",
            prefix,
            static_cast<unsigned long long>(g_ui3d_frame_trace.sequence),
            ordinal, local.translate[0], local.translate[1], local.translate[2],
            local.scale, world.translate[0], world.translate[1],
            world.translate[2], world.scale, bound[0], bound[1], bound[2],
            bound[3],
            std::isfinite(bound[0]) && std::isfinite(bound[1]) &&
                    std::isfinite(bound[2]) && std::isfinite(bound[3])
                ? 1u
                : 0u);
}

void LogUi3DControllerChain(const char* prefix, void* object) {
  void* controller = object && RegionAllows(object, 0x20)
                         ? ReadOr<void*>(
                               reinterpret_cast<uint8_t*>(object) + 0x18,
                               nullptr)
                         : nullptr;
  for (uint32_t index = 0; controller && index < 8; ++index) {
    if (!RegionAllows(controller, 0x48)) break;
    void** vtable = ReadOr<void**>(controller, nullptr);
    void* update = vtable ? ReadOr<void*>(vtable + 0x27, nullptr) : nullptr;
    const uint16_t flags = ReadOr<uint16_t>(
        reinterpret_cast<uint8_t*>(controller) + 0x10, 0);
    const float frequency = ReadOr<float>(
        reinterpret_cast<uint8_t*>(controller) + 0x14, 0.0f);
    const float phase = ReadOr<float>(
        reinterpret_cast<uint8_t*>(controller) + 0x18, 0.0f);
    const float start_time = ReadOr<float>(
        reinterpret_cast<uint8_t*>(controller) + 0x24, 0.0f);
    const float last_time = ReadOr<float>(
        reinterpret_cast<uint8_t*>(controller) + 0x28, 0.0f);
    const float scaled_time = ReadOr<float>(
        reinterpret_cast<uint8_t*>(controller) + 0x30, 0.0f);
    void* target = ReadOr<void*>(
        reinterpret_cast<uint8_t*>(controller) + 0x38, nullptr);
    void* next = ReadOr<void*>(
        reinterpret_cast<uint8_t*>(controller) + 0x40, nullptr);
    PluginLog(false,
              "%s seq=%llu owner=%s index=%u controller=%p vtable=%p "
              "type=%s update=%p flags=%04X active=%u manager=%u "
              "frequency=%.4f phase=%.4f start=%.4f last=%.4f "
              "scaled=%.4f target=%p target_flags=%08X next=%p",
              prefix,
              static_cast<unsigned long long>(g_ui3d_frame_trace.sequence),
              DeferredUi3DTargetOwnerName(g_ui3d_frame_trace.owner), index,
              controller, vtable,
              reinterpret_cast<void*>(vtable) ==
                      reinterpret_cast<void*>(g_base +
                                              kNiVisControllerVtable)
                  ? "NiVisController"
                  : "other",
              update, flags, (flags & (1u << 3)) ? 1u : 0u,
              (flags & (1u << 5)) ? 1u : 0u, frequency, phase, start_time,
              last_time, scaled_time, target,
              target && RegionAllows(target, 0xF8)
                  ? ReadOr<uint32_t>(
                        reinterpret_cast<uint8_t*>(target) + 0xF4, 0)
                  : 0,
              next);
    if (next == controller) break;
    controller = next;
  }
}

void LogUi3DDirectChildren(const char* prefix, void* object) {
  if (!object || !RegionAllows(object, 0x128)) return;
  void* node = SceneVirtualPointer(object, 3);
  if (!node || !RegionAllows(node, 0x128)) return;
  void** child_data = ReadOr<void**>(
      reinterpret_cast<uint8_t*>(node) + 0x118, nullptr);
  const uint16_t child_capacity = ReadOr<uint16_t>(
      reinterpret_cast<uint8_t*>(node) + 0x120, 0);
  const uint16_t child_free_index = ReadOr<uint16_t>(
      reinterpret_cast<uint8_t*>(node) + 0x122, 0);
  if (!child_data || child_capacity == 0 || child_free_index == 0 ||
      child_free_index > child_capacity ||
      !RegionAllows(child_data, sizeof(void*) * child_capacity))
    return;
  const uint16_t limit = child_free_index < 16 ? child_free_index : 16;
  for (uint16_t index = 0; index < limit; ++index) {
    void* child = ReadOr<void*>(child_data + index, nullptr);
    if (child) LogUi3DObjectState(prefix, child, index);
  }
}

bool Ui3DRootContainsChild(void* root, void* child, uint16_t* index_out = nullptr,
                           uint16_t* free_index_out = nullptr,
                           uint16_t* size_out = nullptr) {
  if (index_out) *index_out = 0xFFFF;
  if (free_index_out) *free_index_out = 0;
  if (size_out) *size_out = 0;
  if (!root || !child || !RegionAllows(root, 0x128)) return false;
  void* node = SceneVirtualPointer(root, 3);
  if (!node || !RegionAllows(node, 0x128)) return false;
  void** data = ReadOr<void**>(reinterpret_cast<uint8_t*>(node) + 0x118,
                               nullptr);
  const uint16_t capacity =
      ReadOr<uint16_t>(reinterpret_cast<uint8_t*>(node) + 0x120, 0);
  const uint16_t free_index =
      ReadOr<uint16_t>(reinterpret_cast<uint8_t*>(node) + 0x122, 0);
  const uint16_t size =
      ReadOr<uint16_t>(reinterpret_cast<uint8_t*>(node) + 0x124, 0);
  if (free_index_out) *free_index_out = free_index;
  if (size_out) *size_out = size;
  if (!data || capacity == 0 || free_index == 0 ||
      free_index > capacity || !RegionAllows(data, sizeof(void*) * capacity))
    return false;
  for (uint16_t index = 0; index < free_index; ++index) {
    if (ReadOr<void*>(data + index, nullptr) == child) {
      if (index_out) *index_out = index;
      return true;
    }
  }
  return false;
}

void LogUi3DCameraDetailed(const void* camera, void* culling) {
  if (!camera || !RegionAllows(camera, 0x188)) return;
  SceneTransform local = {};
  SceneTransform world = {};
  float matrix[16] = {};
  float frustum[7] = {};
  float port[4] = {};
  Read(reinterpret_cast<const uint8_t*>(camera) + 0x48, &local);
  Read(reinterpret_cast<const uint8_t*>(camera) + 0x7C, &world);
  Read(reinterpret_cast<const uint8_t*>(camera) + 0x110, &matrix);
  Read(reinterpret_cast<const uint8_t*>(camera) + 0x150, &frustum);
  Read(reinterpret_cast<const uint8_t*>(camera) + 0x174, &port);
  const uint32_t active_planes = culling
                                     ? ReadOr<uint32_t>(
                                           reinterpret_cast<uint8_t*>(culling) +
                                               0x9C,
                                           0)
                                     : 0;
  PluginLog(false,
            "[UI3D_CAMERA] seq=%llu owner=%s camera=%p parent=%p "
            "local=(%.3f,%.3f,%.3f s=%.4f) "
            "world=(%.3f,%.3f,%.3f s=%.4f)",
            static_cast<unsigned long long>(g_ui3d_frame_trace.sequence),
            DeferredUi3DTargetOwnerName(g_ui3d_frame_trace.owner), camera,
            ReadOr<void*>(reinterpret_cast<const uint8_t*>(camera) + 0x30,
                          nullptr),
            local.translate[0], local.translate[1], local.translate[2],
            local.scale, world.translate[0], world.translate[1],
            world.translate[2], world.scale);
  PluginLog(false,
            "[UI3D_CAMERA] seq=%llu basis "
            "forward=(%.4f,%.4f,%.4f) side=(%.4f,%.4f,%.4f) "
            "up=(%.4f,%.4f,%.4f)",
            static_cast<unsigned long long>(g_ui3d_frame_trace.sequence),
            world.rotate[0], world.rotate[3], world.rotate[6], world.rotate[1],
            world.rotate[4], world.rotate[7], world.rotate[2], world.rotate[5],
            world.rotate[8]);
  for (unsigned row = 0; row < 4; ++row) {
    PluginLog(false,
              "[UI3D_CAMERA] seq=%llu world_to_camera[%u]="
              "(%.6f,%.6f,%.6f,%.6f)",
              static_cast<unsigned long long>(g_ui3d_frame_trace.sequence), row,
              matrix[row * 4 + 0], matrix[row * 4 + 1],
              matrix[row * 4 + 2], matrix[row * 4 + 3]);
  }
  PluginLog(false,
            "[UI3D_CAMERA] seq=%llu frustum="
            "(l=%.6f r=%.6f t=%.6f b=%.6f n=%.4f f=%.2f ortho=%.0f) "
            "port=(%.4f,%.4f,%.4f,%.4f) active_planes=%02X",
            static_cast<unsigned long long>(g_ui3d_frame_trace.sequence),
            frustum[0], frustum[1], frustum[2], frustum[3], frustum[4],
            frustum[5], frustum[6], port[0], port[1], port[2], port[3],
            active_planes);
}

void LogUi3DTargetAtTracePoint(const char* point) {
  if (!g_ui3d_frame_trace.active) return;
  IDXGISwapChain* chain =
      g_primary_capture_chain.load(std::memory_order_acquire);
  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* context = nullptr;
  ID3D11RenderTargetView* actual_view = nullptr;
  ID3D11DepthStencilView* actual_depth = nullptr;
  ID3D11DepthStencilState* actual_depth_state = nullptr;
  D3D11_DEPTH_STENCIL_DESC depth_state_desc = {};
  UINT stencil_reference = 0;
  bool depth_state_explicit = false;
  D3D11_VIEWPORT viewport = {};
  UINT viewport_count = 1;
  if (chain && SUCCEEDED(chain->GetDevice(
                   __uuidof(ID3D11Device),
                   reinterpret_cast<void**>(&device))) &&
      device) {
    device->GetImmediateContext(&context);
    device->Release();
  }
  if (context) {
    context->OMGetRenderTargets(1, &actual_view, &actual_depth);
    context->OMGetDepthStencilState(&actual_depth_state, &stencil_reference);
    if (actual_depth_state) {
      actual_depth_state->GetDesc(&depth_state_desc);
      depth_state_explicit = true;
    } else {
      // A null state means D3D11's default state: depth enabled, writes on,
      // LESS comparison, and stencil disabled.
      depth_state_desc.DepthEnable = TRUE;
      depth_state_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
      depth_state_desc.DepthFunc = D3D11_COMPARISON_LESS;
      depth_state_desc.StencilEnable = FALSE;
    }
    context->RSGetViewports(&viewport_count, &viewport);
    context->Release();
  }
  auto* framebuffer = reinterpret_cast<RenderTargetDataRaw*>(
      g_base + kRendererSingleton + kRendererFramebufferOffset);
  auto* target_manager =
      reinterpret_cast<uint8_t*>(g_base + kRenderTargetManager);
  const uint32_t target_width = ReadOr<uint32_t>(
      target_manager + kRendererFramebufferWidthOffset, 0);
  const uint32_t target_height = ReadOr<uint32_t>(
      target_manager + kRendererFramebufferHeightOffset, 0);
  const uint32_t depth_width = ReadOr<uint32_t>(
      target_manager + kRendererDepthStencilWidthOffset, 0);
  const uint32_t depth_height = ReadOr<uint32_t>(
      target_manager + kRendererDepthStencilHeightOffset, 0);
  const int32_t active_target = ReadOr<int32_t>(
      reinterpret_cast<void*>(g_base + kRendererActiveRenderTarget), -999);
  const int32_t active_depth = ReadOr<int32_t>(
      reinterpret_cast<void*>(g_base + kRendererActiveDepthStencil), -999);
  PluginLog(false,
            "[UI3D_TARGET] seq=%llu owner=%s point=%s expected=%p "
            "bound=%p match=%u depth=%p viewport=%.0fx%.0f "
            "renderer_rtv=%p target=%ux%u depth_target=%ux%u "
            "active_target=%d active_depth=%d depth_state=%p explicit=%u "
            "depth_enable=%u depth_write=%u depth_func=%u "
            "stencil_enable=%u stencil_ref=%u",
            static_cast<unsigned long long>(g_ui3d_frame_trace.sequence),
            DeferredUi3DTargetOwnerName(g_ui3d_frame_trace.owner),
            point ? point : "unknown", g_ui3d_frame_trace.expected_view,
            actual_view,
            actual_view == g_ui3d_frame_trace.expected_view ? 1u : 0u,
            actual_depth, viewport.Width, viewport.Height,
            framebuffer && RegionAllows(framebuffer, sizeof(*framebuffer))
                ? framebuffer->render_view
                : nullptr,
            target_width, target_height, depth_width, depth_height,
            active_target, active_depth, actual_depth_state,
            depth_state_explicit ? 1u : 0u,
            depth_state_desc.DepthEnable ? 1u : 0u,
            static_cast<unsigned>(depth_state_desc.DepthWriteMask),
            static_cast<unsigned>(depth_state_desc.DepthFunc),
            depth_state_desc.StencilEnable ? 1u : 0u,
            stencil_reference);
  if (actual_depth_state) actual_depth_state->Release();
  if (actual_depth) actual_depth->Release();
  if (actual_view) actual_view->Release();
}

void Ui3DCullingProcessHook(void* self, const void* camera, void* scene,
                            void* visible_set) {
  Ui3DCullingProcessFn original = reinterpret_cast<Ui3DCullingProcessFn>(
      g_ui3d_culling_original.load(std::memory_order_acquire));
  // Observe Skyrim's exact native culling state without modifying it. Earlier
  // diagnostics forced kIgnoreMultiBounds even though the captured compound
  // frustum was null; that could hide the actual lifecycle failure and was not
  // a valid production fix.
  int32_t native_cull_mode = -1;
  void* compound_frustum = nullptr;
  uint32_t compound_entries = 0;
  uint8_t compound_active = 0;
  if (g_private_ui3d_render_depth && self) {
    native_cull_mode = ReadOr<int32_t>(
        reinterpret_cast<uint8_t*>(self) + 0x30198, -1);
    compound_frustum = ReadOr<void*>(
        reinterpret_cast<uint8_t*>(self) + 0x301A0, nullptr);
    if (compound_frustum && RegionAllows(compound_frustum, 0xC5)) {
      compound_entries = ReadOr<uint32_t>(
          reinterpret_cast<uint8_t*>(compound_frustum) + 0xBC, 0);
      compound_active = ReadOr<uint8_t>(
          reinterpret_cast<uint8_t*>(compound_frustum) + 0xC4, 0);
    }
    if (g_ui3d_frame_trace.active) {
      PluginLog(false,
                "[UI3D_CULL] seq=%llu owner=%s native_mode=%d process=%p "
                "compound=%p entries=%u active=%u",
                static_cast<unsigned long long>(g_ui3d_frame_trace.sequence),
                DeferredUi3DTargetOwnerName(g_private_ui3d_render_owner),
                native_cull_mode, self, compound_frustum, compound_entries,
                static_cast<unsigned>(compound_active));
    }
  }
  if (g_ui3d_frame_trace.active) {
    ++g_ui3d_frame_trace.culling_calls;
    uint64_t visible_before[4] = {};
    if (visible_set && RegionAllows(visible_set, sizeof(visible_before)))
      Read(visible_set, &visible_before);
    PluginLog(false,
              "[UI3D_SCENE] seq=%llu owner=%s culling_begin call=%u "
              "process=%p camera=%p scene=%p expected_scene=%p "
              "visible=%p words=%llX/%llX/%llX/%llX",
              static_cast<unsigned long long>(g_ui3d_frame_trace.sequence),
              DeferredUi3DTargetOwnerName(g_ui3d_frame_trace.owner),
              g_ui3d_frame_trace.culling_calls, self, camera, scene,
              g_ui3d_frame_trace.scene, visible_set,
              static_cast<unsigned long long>(visible_before[0]),
              static_cast<unsigned long long>(visible_before[1]),
              static_cast<unsigned long long>(visible_before[2]),
              static_cast<unsigned long long>(visible_before[3]));
    const int32_t cull_mode = self
                                  ? ReadOr<int32_t>(
                                        reinterpret_cast<uint8_t*>(self) +
                                            0x30198,
                                        -1)
                                  : -1;
    const uint8_t recurse = self
                                ? ReadOr<uint8_t>(
                                      reinterpret_cast<uint8_t*>(self) +
                                          0x301D4,
                                      0xFF)
                                : 0xFF;
    const uint8_t update_accumulate = self
                                          ? ReadOr<uint8_t>(
                                                reinterpret_cast<uint8_t*>(self) +
                                                    0x11D,
                                                0xFF)
                                          : 0xFF;
    PluginLog(false,
              "[UI3D_CULL] seq=%llu owner=%s process=%p "
              "manager_process=%p cull_mode=%d recurse_to_geometry=%u "
              "update_accumulate=%u scene=%p",
              static_cast<unsigned long long>(g_ui3d_frame_trace.sequence),
              DeferredUi3DTargetOwnerName(g_ui3d_frame_trace.owner), self,
              g_ui3d_frame_trace.manager
                  ? ReadOr<void*>(reinterpret_cast<uint8_t*>(
                                      g_ui3d_frame_trace.manager) +
                                      0x08,
                                  nullptr)
                  : nullptr,
              cull_mode, static_cast<unsigned>(recurse),
              static_cast<unsigned>(update_accumulate), scene);
    LogUi3DCameraDetailed(camera, self);
    LogUi3DObjectState("[UI3D_SCENE]", scene, 0);
    LogUi3DDirectChildren("[UI3D_CHILD_BEFORE]", scene);
    LogUi3DControllerChain("[UI3D_SUBJECT_CONTROLLER_AT_CULL]",
                           g_ui3d_frame_trace.scene);
  }
  if (original) original(self, camera, scene, visible_set);
  if (g_ui3d_frame_trace.active) {
    LogUi3DDirectChildren("[UI3D_CHILD_AFTER]", scene);
    uint64_t visible_after[4] = {};
    if (visible_set && RegionAllows(visible_set, sizeof(visible_after)))
      Read(visible_set, &visible_after);
    PluginLog(false,
              "[UI3D_SCENE] seq=%llu owner=%s culling_end call=%u "
              "visible=%p words=%llX/%llX/%llX/%llX",
              static_cast<unsigned long long>(g_ui3d_frame_trace.sequence),
              DeferredUi3DTargetOwnerName(g_ui3d_frame_trace.owner),
              g_ui3d_frame_trace.culling_calls, visible_set,
              static_cast<unsigned long long>(visible_after[0]),
              static_cast<unsigned long long>(visible_after[1]),
              static_cast<unsigned long long>(visible_after[2]),
              static_cast<unsigned long long>(visible_after[3]));
  }
}

void Ui3DCullingProcess1Hook(void* self, void* object, uint32_t arg2) {
  Ui3DCullingProcess1Fn original = reinterpret_cast<Ui3DCullingProcess1Fn>(
      g_ui3d_culling_process1_original.load(std::memory_order_acquire));
  void* previous_object = g_ui3d_current_cull_object;
  uint32_t on_visible_before = 0;
  if (g_ui3d_frame_trace.active && object) {
    g_ui3d_current_cull_object = object;
    on_visible_before = g_ui3d_frame_trace.on_visible_calls;
    const uint32_t ordinal = ++g_ui3d_frame_trace.scene_nodes_traversed;
    const bool geometry = SceneVirtualPointer(object, 7) != nullptr;
    if (geometry) ++g_ui3d_frame_trace.geometry_nodes_encountered;
    float radius = 0.0f;
    float center[3] = {};
    const bool bound_read =
        Read(reinterpret_cast<uint8_t*>(object) + 0xE4, &center) &&
        Read(reinterpret_cast<uint8_t*>(object) + 0xF0, &radius);
    const uint32_t flags = ReadOr<uint32_t>(
        reinterpret_cast<uint8_t*>(object) + 0xF4, 0);
    const uint32_t active_planes = self
                                       ? ReadOr<uint32_t>(
                                             reinterpret_cast<uint8_t*>(self) +
                                                 0x9C,
                                             0)
                                       : 0;
    uint32_t rejected_planes = 0;
    void** culler_vtable = self ? ReadOr<void**>(self, nullptr) : nullptr;
    void* live_slot = culler_vtable
                          ? ReadOr<void*>(culler_vtable + 0x16, nullptr)
                          : nullptr;
    const int32_t mode = self
                             ? ReadOr<int32_t>(
                                   reinterpret_cast<uint8_t*>(self) + 0x30198,
                                   -1)
                             : -1;
    const uint8_t recurse = self
                                ? ReadOr<uint8_t>(
                                      reinterpret_cast<uint8_t*>(self) +
                                          0x301D4,
                                      0xFF)
                                : 0xFF;
    const uint8_t ignore_preprocess = self
                                          ? ReadOr<uint8_t>(
                                                reinterpret_cast<uint8_t*>(self) +
                                                    0x11E,
                                                0xFF)
                                          : 0xFF;
    const uint8_t clear_inside_planes = self
                                            ? ReadOr<uint8_t>(
                                                  reinterpret_cast<uint8_t*>(self) +
                                                      0x120,
                                                  0xFF)
                                            : 0xFF;
    void* compound = self
                         ? ReadOr<void*>(
                               reinterpret_cast<uint8_t*>(self) + 0x301A0,
                               nullptr)
                         : nullptr;
    PluginLog(false,
              "[UI3D_PROCESS1] seq=%llu owner=%s phase=CALL_ORIGINAL "
              "self=%p vtable=%p live_slot16=%p stored_original=%p "
              "expected=%p identity=%u object=%p mode=%d recurse=%u "
              "ignore_preprocess=%u clear_inside=%u active=%02X compound=%p "
              "bound_read=%u center=(%.9g,%.9g,%.9g) radius=%.9g "
              "flags=%08X bits11/12/20/26=%u%u%u%u arg=%08X",
              static_cast<unsigned long long>(g_ui3d_frame_trace.sequence),
              DeferredUi3DTargetOwnerName(g_ui3d_frame_trace.owner), self,
              culler_vtable, live_slot, reinterpret_cast<void*>(original),
              reinterpret_cast<void*>(g_base + kBSCullingProcess1),
              reinterpret_cast<void*>(original) ==
                      reinterpret_cast<void*>(g_base + kBSCullingProcess1)
                  ? 1u
                  : 0u,
              object, mode, static_cast<unsigned>(recurse),
              static_cast<unsigned>(ignore_preprocess),
              static_cast<unsigned>(clear_inside_planes), active_planes,
              compound, bound_read ? 1u : 0u, center[0], center[1], center[2],
              radius, flags, (flags >> 11) & 1u, (flags >> 12) & 1u,
              (flags >> 20) & 1u, (flags >> 26) & 1u, arg2);

    if (self && bound_read &&
        RegionAllows(reinterpret_cast<uint8_t*>(self) + 0x3C, 0x60)) {
      auto float_bits = [](float value) {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
      };
      for (unsigned plane_index = 0; plane_index < 6; ++plane_index) {
        float plane[4] = {};
        const bool plane_read = Read(
            reinterpret_cast<uint8_t*>(self) + 0x3C + plane_index * 0x10,
            &plane);
        // Preserve the executable's floating-point operation order from
        // SkyrimSE+0xC79410. COMISS/JBE also rejects unordered (NaN) results.
        const float distance =
            (plane[1] * center[1] + plane[0] * center[0]) +
            plane[2] * center[2] - plane[3];
        const bool unordered = std::isnan(distance) || std::isnan(radius);
        const bool active = (active_planes & (1u << plane_index)) != 0;
        const bool rejected = active &&
                              (unordered || distance <= -radius);
        const bool would_clear = active && !rejected &&
                                 distance >= radius &&
                                 clear_inside_planes != 0;
        if (rejected)
          rejected_planes |= 1u << plane_index;
        PluginLog(false,
                  "[UI3D_PLANE] seq=%llu owner=%s object=%p plane=%u "
                  "read=%u active=%u coeff=(%.9g,%.9g,%.9g,%.9g) "
                  "bits=(%08X,%08X,%08X,%08X) distance=%.9g bits=%08X "
                  "neg_radius=%.9g unordered=%u reject=%u would_clear=%u",
                  static_cast<unsigned long long>(
                      g_ui3d_frame_trace.sequence),
                  DeferredUi3DTargetOwnerName(g_ui3d_frame_trace.owner),
                  object, plane_index, plane_read ? 1u : 0u,
                  active ? 1u : 0u, plane[0], plane[1], plane[2], plane[3],
                  float_bits(plane[0]), float_bits(plane[1]),
                  float_bits(plane[2]), float_bits(plane[3]), distance,
                  float_bits(distance), -radius, unordered ? 1u : 0u,
                  rejected ? 1u : 0u, would_clear ? 1u : 0u);
      }
      PluginLog(false,
                "[UI3D_SPATIAL] seq=%llu owner=%s object=%p name=\"%.64s\" "
                "active=%02X radius=%.9g simulated_rejected=%02X",
                static_cast<unsigned long long>(
                    g_ui3d_frame_trace.sequence),
                DeferredUi3DTargetOwnerName(g_ui3d_frame_trace.owner),
                object, Ui3DObjectName(object), active_planes, radius,
                rejected_planes);
    }
    if (radius == 0.0f && (flags & (1u << 11)) == 0) {
      ++g_ui3d_frame_trace.zero_bound_rejections;
      PluginLog(false,
                "[UI3D_CULL] seq=%llu owner=%s object=%p name=\"%.72s\" "
                "decision=REJECTED reason=ZERO_WORLD_BOUND_AND_NOT_ALWAYS_DRAW "
                "radius=%.6f flags=%08X arg=%08X",
                static_cast<unsigned long long>(g_ui3d_frame_trace.sequence),
                DeferredUi3DTargetOwnerName(g_ui3d_frame_trace.owner), object,
                Ui3DObjectName(object), radius, flags, arg2);
    }
    if (g_ui3d_frame_trace.tree_logs < 128) {
      ++g_ui3d_frame_trace.tree_logs;
      LogUi3DObjectState("[UI3D_TREE]", object, ordinal);
    }
  }
  if (original) original(self, object, arg2);
  if (g_ui3d_frame_trace.active && object) {
    PluginLog(false,
              "[UI3D_PROCESS1] seq=%llu owner=%s phase=RETURN object=%p "
              "on_visible_delta=%u active_after=%02X",
              static_cast<unsigned long long>(g_ui3d_frame_trace.sequence),
              DeferredUi3DTargetOwnerName(g_ui3d_frame_trace.owner), object,
              g_ui3d_frame_trace.on_visible_calls - on_visible_before,
              self ? ReadOr<uint32_t>(
                         reinterpret_cast<uint8_t*>(self) + 0x9C, 0)
                   : 0);
  }
  g_ui3d_current_cull_object = previous_object;
}

bool Ui3DCullingBoundTestHook(void* self, const void* bound_ptr) {
  Ui3DCullingBoundTestFn original =
      reinterpret_cast<Ui3DCullingBoundTestFn>(
          g_ui3d_culling_bound_test_original.load(std::memory_order_acquire));
  const bool result = original ? original(self, bound_ptr) : false;
  if (!g_ui3d_frame_trace.active || !bound_ptr) return result;

  ++g_ui3d_frame_trace.bound_tests;
  if (result)
    ++g_ui3d_frame_trace.bound_passes;
  else
    ++g_ui3d_frame_trace.bound_rejections;
  float bound[4] = {};
  Read(bound_ptr, &bound);
  float distances[6] = {};
  uint32_t active_planes = 0;
  if (self && RegionAllows(reinterpret_cast<uint8_t*>(self) + 0x3C,
                           0x64)) {
    active_planes = ReadOr<uint32_t>(
        reinterpret_cast<uint8_t*>(self) + 0x9C, 0);
    for (unsigned plane = 0; plane < 6; ++plane) {
      float value[4] = {};
      Read(reinterpret_cast<uint8_t*>(self) + 0x3C + plane * 0x10,
           &value);
      distances[plane] = value[0] * bound[0] + value[1] * bound[1] +
                         value[2] * bound[2] - value[3];
    }
  }
  void* object = g_ui3d_current_cull_object;
  PluginLog(false,
            "[UI3D_BOUND] seq=%llu owner=%s object=%p name=\"%.64s\" "
            "bound=(%.3f,%.3f,%.3f r=%.3f) active=%02X "
            "distance=(%.3f,%.3f,%.3f,%.3f,%.3f,%.3f) result=%s",
            static_cast<unsigned long long>(g_ui3d_frame_trace.sequence),
            DeferredUi3DTargetOwnerName(g_ui3d_frame_trace.owner), object,
            Ui3DObjectName(object), bound[0], bound[1], bound[2], bound[3],
            active_planes, distances[0], distances[1], distances[2],
            distances[3], distances[4], distances[5],
            result ? "VISIBLE" : "REJECTED");
  return result;
}

void Ui3DCullingAppendHook(void* self, void* geometry, uint32_t arg2) {
  Ui3DCullingAppendFn original = reinterpret_cast<Ui3DCullingAppendFn>(
      g_ui3d_culling_append_original.load(std::memory_order_acquire));
  if (g_ui3d_frame_trace.active) {
    const uint32_t ordinal = ++g_ui3d_frame_trace.geometry_appends;
    PluginLog(false,
              "[UI3D_ACCUM] seq=%llu owner=%s append=%u geometry=%p "
              "name=\"%.72s\" arg=%08X renderer_data=%p",
              static_cast<unsigned long long>(g_ui3d_frame_trace.sequence),
              DeferredUi3DTargetOwnerName(g_ui3d_frame_trace.owner), ordinal,
              geometry, Ui3DObjectName(geometry), arg2,
              geometry
                  ? ReadOr<void*>(reinterpret_cast<uint8_t*>(geometry) + 0x138,
                                  nullptr)
                  : nullptr);
  }
  if (original) original(self, geometry, arg2);
}

bool ApplyPrivateUi3DViewportAtSubmission() {
  ID3D11RenderTargetView* expected = g_private_ui3d_target_view;
  ID3D11DepthStencilView* expected_depth = g_private_ui3d_depth_view;
  const uint32_t width = g_private_ui3d_target_width;
  const uint32_t height = g_private_ui3d_target_height;
  if (!g_private_ui3d_render_depth || !expected || !width || !height)
    return false;

  ID3D11Resource* resource = nullptr;
  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* context = nullptr;
  ID3D11RenderTargetView* bound = nullptr;
  ID3D11DepthStencilView* bound_depth = nullptr;
  expected->GetResource(&resource);
  if (resource) {
    resource->GetDevice(&device);
    resource->Release();
  }
  if (device) {
    device->GetImmediateContext(&context);
    device->Release();
  }
  if (!context) return false;
  context->OMGetRenderTargets(1, &bound, &bound_depth);
  bool owns_target = bound == expected &&
                     (!expected_depth || bound_depth == expected_depth);
  bool rebound_target = false;
  // UI3DSceneManager's actor depth-prepass restores the gameplay OM target
  // before draining the final color accumulator. Rebind only inside our
  // synchronous character scope, where the outer renderer transaction owns
  // both borrowed private views and will transfer them to the Present lease.
  // Static Inventory3D rendering keeps its existing native behavior.
  if (!owns_target &&
      g_private_ui3d_render_owner == DeferredUi3DTargetOwner::kCharacter &&
      expected_depth) {
    context->OMSetRenderTargets(1, &expected, expected_depth);
    owns_target = true;
    rebound_target = true;
  }
  if (owns_target) {
    // UI3D updates BSGraphics' lazy viewport cache after our target is bound,
    // but Wine's D3D11 context still carries the gameplay viewport when the
    // accumulator vcall begins. This is the last synchronous boundary before
    // geometry submission, so commit the matching viewport and scissor here.
    const D3D11_VIEWPORT viewport = {
        0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
        0.0f, 1.0f};
    const D3D11_RECT scissor = {
        0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    context->RSSetViewports(1, &viewport);
    context->RSSetScissorRects(1, &scissor);
  }
  if (rebound_target && g_ui3d_frame_trace.active) {
    PluginLog(false,
              "[UI3D_TARGET_REBIND] seq=%llu owner=%s prior=%p/%p "
              "private=%p/%p size=%ux%u",
              static_cast<unsigned long long>(
                  g_ui3d_frame_trace.sequence),
              DeferredUi3DTargetOwnerName(g_private_ui3d_render_owner),
              bound, bound_depth, expected, expected_depth, width, height);
  }
  if (bound_depth) bound_depth->Release();
  if (bound) bound->Release();
  context->Release();
  return owns_target;
}

// BSShaderAccumulator vtable +0x150 is the per-pass submission reached by
// UI3DSceneManager::Render's manager+0x18 and manager+0x10 paths. The actor
// path intentionally reaches it twice; rigid inventory models reach it once.
// UI3D may restore the gameplay viewport before either call, so apply our
// private target dimensions at both real pass boundaries, not only at the
// final +0x158 accumulator drain below.
void Ui3DAccumulatorPassHook(void* self, uint32_t flags) {
  Ui3DAccumulatorSubmitFn original =
      reinterpret_cast<Ui3DAccumulatorSubmitFn>(
          g_ui3d_accumulator_pass_original.load(std::memory_order_acquire));
  const bool private_viewport_applied =
      ApplyPrivateUi3DViewportAtSubmission();
  if (g_ui3d_frame_trace.active) {
    const uint32_t ordinal = ++g_ui3d_frame_trace.accumulator_pass_calls;
    PluginLog(false,
              "[UI3D_PASS] seq=%llu owner=%s pass=%u accumulator=%p "
              "flags=%08X viewport_applied=%u size=%ux%u expected=%p",
              static_cast<unsigned long long>(
                  g_ui3d_frame_trace.sequence),
              DeferredUi3DTargetOwnerName(g_ui3d_frame_trace.owner),
              ordinal, self, flags, private_viewport_applied ? 1u : 0u,
              g_private_ui3d_target_width, g_private_ui3d_target_height,
              g_private_ui3d_target_view);
    LogUi3DTargetAtTracePoint("accumulator_pass");
  }
  if (original) original(self, flags);
}

void Ui3DAccumulatorSubmitHook(void* self, uint32_t flags) {
  Ui3DAccumulatorSubmitFn original =
      reinterpret_cast<Ui3DAccumulatorSubmitFn>(
          g_ui3d_accumulator_submit_original.load(std::memory_order_acquire));
  if (g_ui3d_frame_trace.active) {
    ++g_ui3d_frame_trace.accumulator_calls;
    uint64_t words[6] = {};
    if (self && RegionAllows(self, sizeof(words))) Read(self, &words);
    PluginLog(false,
              "[UI3D_DRAW] seq=%llu owner=%s accumulator_begin call=%u "
              "accumulator=%p flags=%08X words=%llX/%llX/%llX/%llX/%llX/%llX",
              static_cast<unsigned long long>(g_ui3d_frame_trace.sequence),
              DeferredUi3DTargetOwnerName(g_ui3d_frame_trace.owner),
              g_ui3d_frame_trace.accumulator_calls, self, flags,
              static_cast<unsigned long long>(words[0]),
              static_cast<unsigned long long>(words[1]),
              static_cast<unsigned long long>(words[2]),
              static_cast<unsigned long long>(words[3]),
              static_cast<unsigned long long>(words[4]),
              static_cast<unsigned long long>(words[5]));
    LogUi3DTargetAtTracePoint("accumulator_begin");
  }
  const bool private_viewport_applied =
      ApplyPrivateUi3DViewportAtSubmission();
  if (g_ui3d_frame_trace.active) {
    PluginLog(false,
              "[UI3D_VIEWPORT] seq=%llu owner=%s submission_applied=%u "
              "size=%ux%u expected=%p",
              static_cast<unsigned long long>(
                  g_ui3d_frame_trace.sequence),
              DeferredUi3DTargetOwnerName(g_private_ui3d_render_owner),
              private_viewport_applied ? 1u : 0u,
              g_private_ui3d_target_width, g_private_ui3d_target_height,
              g_private_ui3d_target_view);
    LogUi3DTargetAtTracePoint("accumulator_private_viewport");
  }
  if (original) original(self, flags);
  if (g_ui3d_frame_trace.active) {
    LogUi3DTargetAtTracePoint("accumulator_end");
    PluginLog(false,
              "[UI3D_DRAW] seq=%llu owner=%s accumulator_end call=%u",
              static_cast<unsigned long long>(g_ui3d_frame_trace.sequence),
              DeferredUi3DTargetOwnerName(g_ui3d_frame_trace.owner),
              g_ui3d_frame_trace.accumulator_calls);
  }
}

void Ui3DNodeOnVisibleHook(void* object, void* culling_process,
                           uint32_t arg2) {
  Ui3DNodeOnVisibleFn original = reinterpret_cast<Ui3DNodeOnVisibleFn>(
      g_ui3d_node_on_visible_original.load(std::memory_order_acquire));
  if (g_ui3d_frame_trace.active) {
    const uint32_t ordinal = ++g_ui3d_frame_trace.on_visible_calls;
    LogUi3DObjectState("[UI3D_ON_VISIBLE]", object, ordinal);
    LogUi3DDirectChildren("[UI3D_ON_VISIBLE_CHILD]", object);
  }
  if (original) original(object, culling_process, arg2);
}

bool PatchUi3DVtableSlot(void* object, size_t slot, void* replacement,
                         std::atomic<void*>* original_storage) {
  if (!object || !RegionAllows(object, sizeof(void*))) return false;
  void** table = ReadOr<void**>(object, nullptr);
  if (!table || !RegionAllows(table + slot, sizeof(void*))) return false;
  void* current = ReadOr<void*>(table + slot, nullptr);
  if (current == replacement) return original_storage->load() != nullptr;
  if (!RegionAllows(current, 1, true)) return false;
  DWORD old = 0;
  if (!VirtualProtect(table + slot, sizeof(void*), PAGE_EXECUTE_READWRITE,
                      &old))
    return false;
  original_storage->store(current, std::memory_order_release);
  void* previous = InterlockedCompareExchangePointer(
      reinterpret_cast<void* volatile*>(table + slot), replacement, current);
  DWORD ignored = 0;
  VirtualProtect(table + slot, sizeof(void*), old, &ignored);
  FlushInstructionCache(GetCurrentProcess(), table + slot, sizeof(void*));
  return previous == current;
}

bool InstallUi3DTraceHooks(void* manager) {
  if (g_ui3d_trace_hooks_installed.load(std::memory_order_acquire)) return true;
  if (!manager || !RegionAllows(manager, 0x78)) return false;
  void* culling = ReadOr<void*>(reinterpret_cast<uint8_t*>(manager) + 0x08,
                               nullptr);
  void* accumulator = ReadOr<void*>(
      reinterpret_cast<uint8_t*>(manager) + 0x10, nullptr);
  void* scheme_root = nullptr;
  for (int32_t scheme = 0; scheme < 8 && !scheme_root; ++scheme) {
    scheme_root = ReadOr<void*>(
        reinterpret_cast<uint8_t*>(manager) + 0x38 +
            scheme * sizeof(void*),
        nullptr);
  }
  const bool culling_hooked = PatchUi3DVtableSlot(
      culling, 0x17, reinterpret_cast<void*>(&Ui3DCullingProcessHook),
      &g_ui3d_culling_original);
  const bool process1_hooked = PatchUi3DVtableSlot(
      culling, 0x16, reinterpret_cast<void*>(&Ui3DCullingProcess1Hook),
      &g_ui3d_culling_process1_original);
  const bool append_hooked = PatchUi3DVtableSlot(
      culling, 0x18, reinterpret_cast<void*>(&Ui3DCullingAppendHook),
      &g_ui3d_culling_append_original);
  const bool bound_test_hooked = PatchUi3DVtableSlot(
      culling, 0x1C, reinterpret_cast<void*>(&Ui3DCullingBoundTestHook),
      &g_ui3d_culling_bound_test_original);
  const bool accumulator_pass_hooked = PatchUi3DVtableSlot(
      accumulator, 0x2A,
      reinterpret_cast<void*>(&Ui3DAccumulatorPassHook),
      &g_ui3d_accumulator_pass_original);
  const bool accumulator_hooked = PatchUi3DVtableSlot(
      accumulator, 0x2B,
      reinterpret_cast<void*>(&Ui3DAccumulatorSubmitHook),
      &g_ui3d_accumulator_submit_original);
  const bool on_visible_hooked = PatchUi3DVtableSlot(
      scheme_root, 0x34, reinterpret_cast<void*>(&Ui3DNodeOnVisibleHook),
      &g_ui3d_node_on_visible_original);
  const bool installed = culling_hooked && process1_hooked && append_hooked &&
                         bound_test_hooked && accumulator_pass_hooked &&
                         accumulator_hooked && on_visible_hooked;
  g_ui3d_trace_hooks_installed.store(installed, std::memory_order_release);
  PluginLog(false,
            "[UI3D_TRACE] hooks process2=%u process1=%u append=%u bound=%u "
            "culling=%p accumulator_pass=%u accumulator_finish=%u "
            "object=%p on_visible=%u root=%p",
            culling_hooked ? 1u : 0u, process1_hooked ? 1u : 0u,
            append_hooked ? 1u : 0u, bound_test_hooked ? 1u : 0u, culling,
            accumulator_pass_hooked ? 1u : 0u,
            accumulator_hooked ? 1u : 0u, accumulator,
            on_visible_hooked ? 1u : 0u, scheme_root);
  return installed;
}

void BeginUi3DFrameTrace(DeferredUi3DTargetOwner owner, void* manager,
                         void* scene, ID3D11RenderTargetView* expected_view) {
  static uint64_t sequence = 0;
  g_ui3d_frame_trace = {};
  g_ui3d_frame_trace.active = true;
  g_ui3d_frame_trace.owner = owner;
  g_ui3d_frame_trace.sequence = ++sequence;
  g_ui3d_frame_trace.manager = manager;
  g_ui3d_frame_trace.scene = scene;
  g_ui3d_frame_trace.expected_view = expected_view;
  PluginLog(false,
            "[UI3D_TRACE] seq=%llu owner=%s dispatch_begin manager=%p "
            "scene=%p culling=%p accumulator0=%p accumulator1=%p "
            "camera=%p scheme=%d",
            static_cast<unsigned long long>(g_ui3d_frame_trace.sequence),
            DeferredUi3DTargetOwnerName(owner), manager, scene,
            manager ? ReadOr<void*>(reinterpret_cast<uint8_t*>(manager) + 0x08,
                                    nullptr)
                    : nullptr,
            manager ? ReadOr<void*>(reinterpret_cast<uint8_t*>(manager) + 0x10,
                                    nullptr)
                    : nullptr,
            manager ? ReadOr<void*>(reinterpret_cast<uint8_t*>(manager) + 0x18,
                                    nullptr)
                    : nullptr,
            manager ? ReadOr<void*>(reinterpret_cast<uint8_t*>(manager) + 0x20,
                                    nullptr)
                    : nullptr,
            manager ? ReadOr<int32_t>(reinterpret_cast<uint8_t*>(manager) + 0x90,
                                      -1)
                    : -1);
  LogUi3DTargetAtTracePoint("dispatch_begin");
}

void EndUi3DFrameTrace() {
  if (!g_ui3d_frame_trace.active) return;
  LogUi3DTargetAtTracePoint("dispatch_end");
  PluginLog(false,
            "[UI3D_TRACE] seq=%llu owner=%s dispatch_end "
            "culling_calls=%u traversed=%u geometry=%u bound_tests=%u "
            "bound_pass=%u bound_reject=%u zero_bound_reject=%u "
            "appended=%u pass_calls=%u finish_calls=%u "
            "geometry_branch=%s",
            static_cast<unsigned long long>(g_ui3d_frame_trace.sequence),
            DeferredUi3DTargetOwnerName(g_ui3d_frame_trace.owner),
            g_ui3d_frame_trace.culling_calls,
            g_ui3d_frame_trace.scene_nodes_traversed,
            g_ui3d_frame_trace.geometry_nodes_encountered,
            g_ui3d_frame_trace.bound_tests,
            g_ui3d_frame_trace.bound_passes,
            g_ui3d_frame_trace.bound_rejections,
            g_ui3d_frame_trace.zero_bound_rejections,
            g_ui3d_frame_trace.geometry_appends,
            g_ui3d_frame_trace.accumulator_pass_calls,
            g_ui3d_frame_trace.accumulator_calls,
            g_ui3d_frame_trace.accumulator_calls ? "ENTERED" : "SKIPPED");
  g_ui3d_frame_trace.active = false;
}

// Actor UI3D work is deferred beyond UI3DSceneManager::Render on Skyrim
// 1.5.97. Historical captures with this exact lease produced authoritative
// character depth, while immediate framebuffer restoration consistently
// produced an empty target after otherwise identical culling/append traces.
bool SameRenderTargetData(const RenderTargetDataRaw& left,
                          const RenderTargetDataRaw& right) {
  return left.texture == right.texture &&
         left.texture_copy == right.texture_copy &&
         left.render_view == right.render_view &&
         left.shader_view == right.shader_view &&
         left.shader_copy == right.shader_copy &&
         left.unordered_view == right.unordered_view;
}

void RetainRenderTargetData(RenderTargetDataRaw& target) {
  // RendererTargetData itself contains raw COM pointers. Retain one reference
  // per stored field (including aliases) for the complete deferred lease so a
  // resize/device transition cannot free the exact Skyrim state we may need to
  // republish on the origin device.
  if (target.texture) target.texture->AddRef();
  if (target.texture_copy) target.texture_copy->AddRef();
  if (target.render_view) target.render_view->AddRef();
  if (target.shader_view) target.shader_view->AddRef();
  if (target.shader_copy) target.shader_copy->AddRef();
  if (target.unordered_view) target.unordered_view->AddRef();
}

void ReleaseRetainedRenderTargetData(RenderTargetDataRaw& target) {
  if (target.unordered_view) target.unordered_view->Release();
  if (target.shader_copy) target.shader_copy->Release();
  if (target.shader_view) target.shader_view->Release();
  if (target.render_view) target.render_view->Release();
  if (target.texture_copy) target.texture_copy->Release();
  if (target.texture) target.texture->Release();
  target = {};
}

void ReleaseDeferredUi3DTargetLease(DeferredUi3DTargetLease& lease) {
  // Make reentrant cleanup a no-op before any COM destructor can run.
  lease.active = false;
  if (lease.pipeline_query) lease.pipeline_query->Release();
  if (lease.occlusion_query) lease.occlusion_query->Release();
  if (lease.old_depth) lease.old_depth->Release();
  for (auto* view : lease.old_views)
    if (view) view->Release();
  if (lease.expected_depth) lease.expected_depth->Release();
  for (auto* view : lease.expected_views)
    if (view) view->Release();
  ReleaseRetainedRenderTargetData(lease.saved_framebuffer);
  if (lease.origin_device) lease.origin_device->Release();
  lease = {};
}

bool ArmDeferredUi3DTarget(
    DeferredUi3DTargetOwner owner, RenderTargetDataRaw* framebuffer,
    const RenderTargetDataRaw& saved_framebuffer, uint32_t* framebuffer_width,
    uint32_t saved_framebuffer_width, uint32_t* framebuffer_height,
    uint32_t saved_framebuffer_height, uint32_t* depth_stencil_width,
    uint32_t saved_depth_stencil_width, uint32_t* depth_stencil_height,
    uint32_t saved_depth_stencil_height,
    ID3D11RenderTargetView* const* old_views,
    ID3D11DepthStencilView* old_depth, UINT viewport_count,
    const D3D11_VIEWPORT* old_viewports, UINT scissor_count,
    const D3D11_RECT* old_scissors, ID3D11Query* pipeline_query = nullptr,
    ID3D11Query* occlusion_query = nullptr) {
  if (g_deferred_ui3d_target.active ||
      owner == DeferredUi3DTargetOwner::kNone || !framebuffer ||
      !framebuffer_width || !framebuffer_height || !depth_stencil_width ||
      !depth_stencil_height)
    return false;
  if (!RegionAllows(framebuffer, sizeof(*framebuffer)) ||
      !RegionWritable(framebuffer, sizeof(*framebuffer)) ||
      !RegionAllows(framebuffer_width, sizeof(*framebuffer_width)) ||
      !RegionWritable(framebuffer_width, sizeof(*framebuffer_width)) ||
      !RegionAllows(framebuffer_height, sizeof(*framebuffer_height)) ||
      !RegionWritable(framebuffer_height, sizeof(*framebuffer_height)) ||
      !RegionAllows(depth_stencil_width, sizeof(*depth_stencil_width)) ||
      !RegionWritable(depth_stencil_width, sizeof(*depth_stencil_width)) ||
      !RegionAllows(depth_stencil_height, sizeof(*depth_stencil_height)) ||
      !RegionWritable(depth_stencil_height, sizeof(*depth_stencil_height)) ||
      !framebuffer->render_view)
    return false;
  ID3D11Device* origin_device = nullptr;
  framebuffer->render_view->GetDevice(&origin_device);
  if (!origin_device) return false;
  auto& lease = g_deferred_ui3d_target;
  lease = {};
  lease.owner = owner;
  lease.origin_device = origin_device;
  ID3D11DeviceContext* origin_context = nullptr;
  origin_device->GetImmediateContext(&origin_context);
  if (!origin_context) {
    ReleaseDeferredUi3DTargetLease(lease);
    return false;
  }
  origin_context->OMGetRenderTargets(
      D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, lease.expected_views,
      &lease.expected_depth);
  lease.expected_viewport_count =
      D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
  origin_context->RSGetViewports(&lease.expected_viewport_count,
                                 lease.expected_viewports);
  lease.expected_scissor_count =
      D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
  origin_context->RSGetScissorRects(&lease.expected_scissor_count,
                                    lease.expected_scissors);
  origin_context->Release();
  // The metadata points at the private RTV, so slot zero must describe that
  // same pass when the lease is armed. Otherwise there is no exact pipeline
  // state which Present can later prove it still owns.
  if (lease.expected_views[0] != framebuffer->render_view) {
    ReleaseDeferredUi3DTargetLease(lease);
    return false;
  }
  lease.framebuffer = framebuffer;
  lease.saved_framebuffer = saved_framebuffer;
  RetainRenderTargetData(lease.saved_framebuffer);
  lease.expected_framebuffer = *framebuffer;
  lease.framebuffer_width = framebuffer_width;
  lease.framebuffer_height = framebuffer_height;
  lease.depth_stencil_width = depth_stencil_width;
  lease.depth_stencil_height = depth_stencil_height;
  lease.saved_framebuffer_width = saved_framebuffer_width;
  lease.saved_framebuffer_height = saved_framebuffer_height;
  lease.saved_depth_stencil_width = saved_depth_stencil_width;
  lease.saved_depth_stencil_height = saved_depth_stencil_height;
  lease.expected_framebuffer_width = *framebuffer_width;
  lease.expected_framebuffer_height = *framebuffer_height;
  lease.expected_depth_stencil_width = *depth_stencil_width;
  lease.expected_depth_stencil_height = *depth_stencil_height;
  for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    lease.old_views[i] = old_views ? old_views[i] : nullptr;
  lease.old_depth = old_depth;
  lease.viewport_count = std::min<UINT>(
      viewport_count,
      D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
  for (UINT i = 0; i < lease.viewport_count; ++i)
    lease.old_viewports[i] = old_viewports[i];
  lease.scissor_count = std::min<UINT>(
      scissor_count,
      D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
  for (UINT i = 0; i < lease.scissor_count; ++i)
    lease.old_scissors[i] = old_scissors[i];
  lease.pipeline_query = pipeline_query;
  lease.occlusion_query = occlusion_query;
  lease.trace_sequence = g_ui3d_frame_trace.sequence;
  lease.armed_ms = GetTickCount64();
  // Publish active last. Present must never observe a partially initialized
  // lease even if callback scheduling changes on another Wine/DXVK build.
  lease.active = true;
  return true;
}

void RestoreDeferredUi3DTarget(IDXGISwapChain* chain,
                               const char* reason = "Present") {
  auto& lease = g_deferred_ui3d_target;
  if (!lease.active) return;
  const DeferredUi3DTargetOwner owner = lease.owner;
  const uint64_t age = lease.armed_ms ? GetTickCount64() - lease.armed_ms : 0;
  const bool diagnostic = lease.pipeline_query || lease.occlusion_query;
  bool light_restore_ok = true;
  if (lease.light_override_active) {
    using SetLightSchemeFn = void(*)(void*, int32_t);
    auto set_light_scheme = reinterpret_cast<SetLightSchemeFn>(
        g_base + kUI3DSetCurrentLightScheme);
    const int32_t observed = ReadOr<int32_t>(
        lease.current_light_scheme, INT32_MIN);
    if (!lease.light_manager ||
        !RegionAllows(lease.light_manager, 0x94) ||
        !lease.current_light_scheme ||
        !RegionWritable(lease.current_light_scheme,
                        sizeof(*lease.current_light_scheme)) ||
        !RegionAllows(reinterpret_cast<void*>(set_light_scheme), 1, true) ||
        lease.saved_light_scheme < 0 || lease.saved_light_scheme >= 8 ||
        lease.active_light_scheme < 0 || lease.active_light_scheme >= 8) {
      light_restore_ok = false;
    } else {
      // SetCurrentLightScheme removes the set named by the manager field but
      // does not write that field. The deferred actor used Inventory lights
      // while scene 5 was the dispatch selector, so restore the real active
      // identity before asking Skyrim to return to its saved set.
      light_restore_ok =
          WriteValue(lease.current_light_scheme,
                     lease.active_light_scheme);
      if (light_restore_ok &&
          lease.saved_light_scheme != lease.active_light_scheme)
        set_light_scheme(lease.light_manager, lease.saved_light_scheme);
      light_restore_ok =
          WriteValue(lease.current_light_scheme,
                     lease.saved_light_scheme) && light_restore_ok;
    }
    PluginLog(false,
              "[CHAR_LIGHT] deferred restore observed=%d expected=%d "
              "active=%d saved=%d ok=%u age=%llums",
              observed, lease.dispatch_light_scheme,
              lease.active_light_scheme, lease.saved_light_scheme,
              light_restore_ok ? 1u : 0u,
              static_cast<unsigned long long>(age));
    lease.light_override_active = false;
  }
  const bool storage_valid =
      lease.framebuffer &&
      RegionAllows(lease.framebuffer, sizeof(*lease.framebuffer)) &&
      RegionWritable(lease.framebuffer, sizeof(*lease.framebuffer)) &&
      lease.framebuffer_width &&
      RegionAllows(lease.framebuffer_width, sizeof(*lease.framebuffer_width)) &&
      RegionWritable(lease.framebuffer_width,
                     sizeof(*lease.framebuffer_width)) &&
      lease.framebuffer_height &&
      RegionAllows(lease.framebuffer_height,
                   sizeof(*lease.framebuffer_height)) &&
      RegionWritable(lease.framebuffer_height,
                     sizeof(*lease.framebuffer_height)) &&
      lease.depth_stencil_width &&
      RegionAllows(lease.depth_stencil_width,
                   sizeof(*lease.depth_stencil_width)) &&
      RegionWritable(lease.depth_stencil_width,
                     sizeof(*lease.depth_stencil_width)) &&
      lease.depth_stencil_height &&
      RegionAllows(lease.depth_stencil_height,
                   sizeof(*lease.depth_stencil_height)) &&
      RegionWritable(lease.depth_stencil_height,
                     sizeof(*lease.depth_stencil_height));
  RenderTargetDataRaw current_framebuffer = {};
  uint32_t current_framebuffer_width = 0;
  uint32_t current_framebuffer_height = 0;
  uint32_t current_depth_stencil_width = 0;
  uint32_t current_depth_stencil_height = 0;
  if (storage_valid) {
    current_framebuffer = *lease.framebuffer;
    current_framebuffer_width = *lease.framebuffer_width;
    current_framebuffer_height = *lease.framebuffer_height;
    current_depth_stencil_width = *lease.depth_stencil_width;
    current_depth_stencil_height = *lease.depth_stencil_height;
  }
  const bool metadata_exact =
      storage_valid &&
      SameRenderTargetData(current_framebuffer, lease.expected_framebuffer) &&
      current_framebuffer_width == lease.expected_framebuffer_width &&
      current_framebuffer_height == lease.expected_framebuffer_height &&
      current_depth_stencil_width == lease.expected_depth_stencil_width &&
      current_depth_stencil_height == lease.expected_depth_stencil_height;
  const bool metadata_pointer_owned[6] = {
      storage_valid && lease.expected_framebuffer.texture &&
          current_framebuffer.texture == lease.expected_framebuffer.texture,
      storage_valid && lease.expected_framebuffer.texture_copy &&
          current_framebuffer.texture_copy ==
              lease.expected_framebuffer.texture_copy,
      storage_valid && lease.expected_framebuffer.render_view &&
          current_framebuffer.render_view ==
              lease.expected_framebuffer.render_view,
      storage_valid && lease.expected_framebuffer.shader_view &&
          current_framebuffer.shader_view ==
              lease.expected_framebuffer.shader_view,
      storage_valid && lease.expected_framebuffer.shader_copy &&
          current_framebuffer.shader_copy ==
              lease.expected_framebuffer.shader_copy,
      storage_valid && lease.expected_framebuffer.unordered_view &&
          current_framebuffer.unordered_view ==
              lease.expected_framebuffer.unordered_view,
  };
  bool metadata_any_owned = false;
  for (bool owned : metadata_pointer_owned)
    metadata_any_owned = metadata_any_owned || owned;
  ID3D11RenderTargetView* const leased_renderer_view =
      lease.expected_framebuffer.render_view;
  const int32_t active_target_before = ReadOr<int32_t>(
      reinterpret_cast<void*>(g_base + kRendererActiveRenderTarget), -999);
  const int32_t active_depth_before = ReadOr<int32_t>(
      reinterpret_cast<void*>(g_base + kRendererActiveDepthStencil), -999);
  ID3D11Device* chain_device = nullptr;
  const HRESULT chain_device_result =
      chain ? chain->GetDevice(__uuidof(ID3D11Device),
                               reinterpret_cast<void**>(&chain_device))
            : E_POINTER;
  const bool chain_matches_origin =
      SUCCEEDED(chain_device_result) && chain_device && lease.origin_device &&
      chain_device == lease.origin_device;
  ID3D11DeviceContext* context = nullptr;
  if (lease.origin_device) lease.origin_device->GetImmediateContext(&context);
  ID3D11RenderTargetView* current_views[
      D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
  ID3D11DepthStencilView* current_depth = nullptr;
  UINT current_viewport_count =
      D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
  D3D11_VIEWPORT current_viewports[
      D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
  UINT current_scissor_count =
      D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
  D3D11_RECT current_scissors[
      D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
  if (context) {
    context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
                                current_views, &current_depth);
    context->RSGetViewports(&current_viewport_count, current_viewports);
    context->RSGetScissorRects(&current_scissor_count, current_scissors);
  } else {
    current_viewport_count = 0;
    current_scissor_count = 0;
  }
  bool render_targets_match = context != nullptr &&
                               current_depth == lease.expected_depth;
  for (UINT i = 0;
       render_targets_match && i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
       ++i)
    render_targets_match = current_views[i] == lease.expected_views[i];
  const bool viewports_match =
      context && current_viewport_count == lease.expected_viewport_count &&
      std::memcmp(current_viewports, lease.expected_viewports,
                  static_cast<size_t>(current_viewport_count) *
                      sizeof(D3D11_VIEWPORT)) == 0;
  const bool scissors_match =
      context && current_scissor_count == lease.expected_scissor_count &&
      std::memcmp(current_scissors, lease.expected_scissors,
                  static_cast<size_t>(current_scissor_count) *
                      sizeof(D3D11_RECT)) == 0;
  const bool pipeline_exact = render_targets_match && viewports_match &&
                              scissors_match;
  bool om_any_owned = false;
  if (context) {
    if (lease.expected_depth && current_depth == lease.expected_depth)
      om_any_owned = true;
    for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
      if (lease.expected_views[i] &&
          current_views[i] == lease.expected_views[i]) {
        om_any_owned = true;
        break;
      }
    }
  }

  // Roll back exact-owned components independently. A concurrent renderer
  // transition may legitimately replace only one field; preserving that new
  // field while removing every remaining WEMU pointer avoids both clobbering
  // Skyrim state and leaving a private resource published. On a device change,
  // remove our pointers but never publish retained objects from the old device.
  if (storage_valid && metadata_any_owned) {
    RenderTargetDataRaw restored = current_framebuffer;
    auto restore_pointer = [&](auto*& current, auto* expected, auto* saved,
                               bool owned) {
      if (owned || (!expected && !current && metadata_any_owned))
        current = chain_matches_origin ? saved : nullptr;
    };
    restore_pointer(restored.texture, lease.expected_framebuffer.texture,
                    lease.saved_framebuffer.texture,
                    metadata_pointer_owned[0]);
    restore_pointer(restored.texture_copy,
                    lease.expected_framebuffer.texture_copy,
                    lease.saved_framebuffer.texture_copy,
                    metadata_pointer_owned[1]);
    restore_pointer(restored.render_view,
                    lease.expected_framebuffer.render_view,
                    lease.saved_framebuffer.render_view,
                    metadata_pointer_owned[2]);
    restore_pointer(restored.shader_view,
                    lease.expected_framebuffer.shader_view,
                    lease.saved_framebuffer.shader_view,
                    metadata_pointer_owned[3]);
    restore_pointer(restored.shader_copy,
                    lease.expected_framebuffer.shader_copy,
                    lease.saved_framebuffer.shader_copy,
                    metadata_pointer_owned[4]);
    restore_pointer(restored.unordered_view,
                    lease.expected_framebuffer.unordered_view,
                    lease.saved_framebuffer.unordered_view,
                    metadata_pointer_owned[5]);
    *lease.framebuffer = restored;
    if (chain_matches_origin) {
      if (current_framebuffer_width == lease.expected_framebuffer_width)
        *lease.framebuffer_width = lease.saved_framebuffer_width;
      if (current_framebuffer_height == lease.expected_framebuffer_height)
        *lease.framebuffer_height = lease.saved_framebuffer_height;
      if (current_depth_stencil_width ==
          lease.expected_depth_stencil_width)
        *lease.depth_stencil_width = lease.saved_depth_stencil_width;
      if (current_depth_stencil_height ==
          lease.expected_depth_stencil_height)
        *lease.depth_stencil_height = lease.saved_depth_stencil_height;
    }
  }

  if (diagnostic && context) {
    const D3D11_VIEWPORT viewport_before =
        current_viewport_count ? current_viewports[0] : D3D11_VIEWPORT{};
    PluginLog(false,
              "[UI3D_PRESENT] seq=%llu owner=%s reason=%s age=%llums "
              "leased_renderer_rtv=%p bound_before_restore=%p match=%u "
              "depth=%p viewport=%.0fx%.0f active_target=%d active_depth=%d "
              "metadata_exact=%u pipeline_exact=%u metadata_token=%u "
              "om_token=%u chain_matches=%u",
              static_cast<unsigned long long>(lease.trace_sequence),
              DeferredUi3DTargetOwnerName(owner),
              reason ? reason : "unknown",
              static_cast<unsigned long long>(age), leased_renderer_view,
              current_views[0],
              current_views[0] == lease.expected_views[0] ? 1u : 0u,
              current_depth, viewport_before.Width, viewport_before.Height,
              active_target_before, active_depth_before,
              metadata_exact ? 1u : 0u, pipeline_exact ? 1u : 0u,
              metadata_any_owned ? 1u : 0u, om_any_owned ? 1u : 0u,
              chain_matches_origin ? 1u : 0u);
  }

  D3D11_QUERY_DATA_PIPELINE_STATISTICS statistics = {};
  UINT64 visible_samples = 0;
  HRESULT pipeline_result = E_NOINTERFACE;
  HRESULT occlusion_result = E_NOINTERFACE;
  if (context && lease.pipeline_query) {
      pipeline_result = context->GetData(
          lease.pipeline_query, &statistics, sizeof(statistics),
          D3D11_ASYNC_GETDATA_DONOTFLUSH);
  }
  if (context && lease.occlusion_query) {
      occlusion_result = context->GetData(
          lease.occlusion_query, &visible_samples, sizeof(visible_samples),
          D3D11_ASYNC_GETDATA_DONOTFLUSH);
  }
  if (context &&
      (pipeline_result == S_FALSE || occlusion_result == S_FALSE) &&
      (lease.pipeline_query || lease.occlusion_query)) {
      context->Flush();
      for (unsigned poll = 0; poll < 4 &&
                              (pipeline_result == S_FALSE ||
                               occlusion_result == S_FALSE);
           ++poll) {
        if (lease.pipeline_query && pipeline_result == S_FALSE)
          pipeline_result = context->GetData(
              lease.pipeline_query, &statistics, sizeof(statistics), 0);
        if (lease.occlusion_query && occlusion_result == S_FALSE)
          occlusion_result = context->GetData(
              lease.occlusion_query, &visible_samples,
              sizeof(visible_samples), 0);
        if (pipeline_result == S_FALSE || occlusion_result == S_FALSE)
          SwitchToThread();
      }
  }
  if (diagnostic)
    PluginLog(false,
              "[UI3D_DRAW] seq=%llu owner=%s present_query "
              "pipeline_hr=%08lX vertices=%llu primitives=%llu vs=%llu "
              "ps=%llu rendered=%llu occlusion_hr=%08lX samples=%llu",
              static_cast<unsigned long long>(lease.trace_sequence),
              DeferredUi3DTargetOwnerName(owner),
              static_cast<unsigned long>(pipeline_result),
              static_cast<unsigned long long>(statistics.IAVertices),
              static_cast<unsigned long long>(statistics.IAPrimitives),
              static_cast<unsigned long long>(statistics.VSInvocations),
              static_cast<unsigned long long>(statistics.PSInvocations),
              static_cast<unsigned long long>(statistics.CPrimitives),
              static_cast<unsigned long>(occlusion_result),
              static_cast<unsigned long long>(visible_samples));
  bool om_components_restored = false;
  if (context && om_any_owned) {
    ID3D11RenderTargetView* restored_views[
        D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
      const bool exact_owned = lease.expected_views[i] &&
          current_views[i] == lease.expected_views[i];
      const bool null_owned = !lease.expected_views[i] && !current_views[i];
      restored_views[i] = (exact_owned || null_owned)
                              ? lease.old_views[i]
                              : current_views[i];
      om_components_restored = om_components_restored || exact_owned ||
                               (null_owned && restored_views[i]);
    }
    const bool depth_owned =
        (lease.expected_depth && current_depth == lease.expected_depth) ||
        (!lease.expected_depth && !current_depth);
    ID3D11DepthStencilView* restored_depth =
        depth_owned ? lease.old_depth : current_depth;
    om_components_restored = om_components_restored ||
                             (lease.expected_depth && depth_owned) ||
                             (!lease.expected_depth && !current_depth &&
                              restored_depth);
    context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
                                restored_views, restored_depth);
    // RS state has no pointer identity. Restore it only while a non-null exact
    // private OM binding supplied the strong ownership token, and only if the
    // complete corresponding RS array is still byte-for-byte ours.
    if (viewports_match) {
      context->RSSetViewports(
          lease.viewport_count,
          lease.viewport_count ? lease.old_viewports : nullptr);
    }
    if (scissors_match) {
      context->RSSetScissorRects(
          lease.scissor_count,
          lease.scissor_count ? lease.old_scissors : nullptr);
    }
  }
  ID3D11RenderTargetView* bound_after = nullptr;
  if (context) context->OMGetRenderTargets(1, &bound_after, nullptr);
  if (context && (diagnostic ||
                  (om_components_restored &&
                   bound_after != lease.old_views[0])))
    PluginLog(false,
              "[UI3D_STATE] seq=%llu owner=%s restore_complete "
              "bound=%p expected_main=%p match=%u",
              static_cast<unsigned long long>(lease.trace_sequence),
              DeferredUi3DTargetOwnerName(owner), bound_after,
              lease.old_views[0],
              om_components_restored && bound_after == lease.old_views[0]
                  ? 1u
                  : 0u);
  if (bound_after) bound_after->Release();
  const bool ownership_loss = !metadata_exact || !pipeline_exact ||
                               !chain_matches_origin || !light_restore_ok;
  if (ownership_loss) {
    const HRESULT removed_reason = lease.origin_device
                                       ? lease.origin_device->GetDeviceRemovedReason()
                                       : E_POINTER;
    PluginLog(
        false,
        "[GPU_OWNERSHIP] [%s] deferred framebuffer rollback at %s "
        "age=%llums storage=%u metadata_exact=%u pipeline_exact=%u "
        "metadata_token=%u om_token=%u "
        "rt_match=%u viewport_match=%u scissor_match=%u chain_matches=%u "
        "origin_device=%p chain_device=%p chain_hr=%08lX removed_hr=%08lX "
        "expected_rtv=%p current_rtv=%p expected_size=%ux%u/%ux%u "
        "current_size=%ux%u/%ux%u",
        DeferredUi3DTargetOwnerName(owner), reason ? reason : "unknown",
        static_cast<unsigned long long>(age), storage_valid ? 1u : 0u,
        metadata_exact ? 1u : 0u, pipeline_exact ? 1u : 0u,
        metadata_any_owned ? 1u : 0u, om_any_owned ? 1u : 0u,
        render_targets_match ? 1u : 0u, viewports_match ? 1u : 0u,
        scissors_match ? 1u : 0u, chain_matches_origin ? 1u : 0u,
        lease.origin_device, chain_device,
        static_cast<unsigned long>(chain_device_result),
        static_cast<unsigned long>(removed_reason),
        lease.expected_framebuffer.render_view,
        current_framebuffer.render_view,
        lease.expected_framebuffer_width, lease.expected_framebuffer_height,
        lease.expected_depth_stencil_width,
        lease.expected_depth_stencil_height, current_framebuffer_width,
        current_framebuffer_height, current_depth_stencil_width,
        current_depth_stencil_height);
  }
  if (current_depth) current_depth->Release();
  for (auto* view : current_views)
    if (view) view->Release();
  if (context) context->Release();
  if (chain_device) chain_device->Release();
  ReleaseDeferredUi3DTargetLease(lease);
  if (ownership_loss)
    g_native_render_reset_revision.fetch_add(1, std::memory_order_acq_rel);
  if (diagnostic && !ownership_loss)
    PluginLog(false,
              "[GPU] [%s] deferred framebuffer lease restored at %s age=%llums",
              DeferredUi3DTargetOwnerName(owner), reason ? reason : "unknown",
              static_cast<unsigned long long>(age));
}

void LogGpuOutputBinding(const char* subsystem, ID3D11DeviceContext* context,
                         ID3D11RenderTargetView* expected_view,
                         ID3D11Texture2D* expected_texture,
                         uint32_t metadata_width,
                         uint32_t metadata_height) {
  if (!context) return;
  ID3D11RenderTargetView* actual_view = nullptr;
  ID3D11DepthStencilView* actual_depth = nullptr;
  context->OMGetRenderTargets(1, &actual_view, &actual_depth);
  UINT viewport_count = 1;
  D3D11_VIEWPORT viewport = {};
  context->RSGetViewports(&viewport_count, &viewport);
  ID3D11Resource* resource = nullptr;
  ID3D11Texture2D* texture = nullptr;
  D3D11_TEXTURE2D_DESC description = {};
  if (actual_view) actual_view->GetResource(&resource);
  if (resource && SUCCEEDED(resource->QueryInterface(
                      __uuidof(ID3D11Texture2D),
                      reinterpret_cast<void**>(&texture))) &&
      texture) {
    texture->GetDesc(&description);
  }
  PluginLog(false,
            "[GPU] %s output expected_view=%p actual_view=%p match=%u "
            "expected_texture=%p actual_texture=%p size=%ux%u "
            "metadata=%ux%u viewport=%.0fx%.0f depth=%p",
            subsystem ? subsystem : "UI3D", expected_view, actual_view,
            actual_view == expected_view ? 1u : 0u, expected_texture, texture,
            description.Width, description.Height, metadata_width,
            metadata_height, viewport.Width, viewport.Height, actual_depth);
  if (texture) texture->Release();
  if (resource) resource->Release();
  if (actual_depth) actual_depth->Release();
  if (actual_view) actual_view->Release();
}

// A valid return from UI3DSceneManager::Render only means its high-level pass
// completed.  It does not prove that the culling/accumulator stage submitted
// any geometry.  This bounded one-shot D3D query distinguishes an empty scene
// traversal from a target/readback problem without hooking the device-context
// vtable or logging every Draw call.
class GpuPipelineProbe {
 public:
  void Start(ID3D11Device* device, ID3D11DeviceContext* context) {
    if (!device || !context || query_ || occlusion_query_) return;
    D3D11_QUERY_DESC description = {};
    description.Query = D3D11_QUERY_PIPELINE_STATISTICS;
    if (SUCCEEDED(device->CreateQuery(&description, &query_)) && query_) {
      context->Begin(query_);
    }
    description = {};
    description.Query = D3D11_QUERY_OCCLUSION;
    if (SUCCEEDED(device->CreateQuery(&description, &occlusion_query_)) &&
        occlusion_query_)
      context->Begin(occlusion_query_);
    active_ = query_ || occlusion_query_;
  }

  void Stop(ID3D11DeviceContext* context) {
    if (!active_ || !context) return;
    if (occlusion_query_) context->End(occlusion_query_);
    if (query_) context->End(query_);
    active_ = false;
    ended_ = true;
  }

  void Detach(ID3D11Query** pipeline_query,
              ID3D11Query** occlusion_query) {
    if (pipeline_query) *pipeline_query = query_;
    if (occlusion_query) *occlusion_query = occlusion_query_;
    query_ = nullptr;
    occlusion_query_ = nullptr;
    ended_ = false;
  }

  void Log(ID3D11DeviceContext* context, const char* subsystem) {
    if (!context || !query_ || !ended_) return;
    D3D11_QUERY_DATA_PIPELINE_STATISTICS statistics = {};
    const HRESULT result = context->GetData(
        query_, &statistics, sizeof(statistics),
        D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if (result == S_OK) {
      PluginLog(false,
                "[GPU] %s pipeline ia_vertices=%llu ia_primitives=%llu "
                "vs=%llu ps=%llu gs=%llu rendered_primitives=%llu",
                subsystem ? subsystem : "UI3D",
                static_cast<unsigned long long>(statistics.IAVertices),
                static_cast<unsigned long long>(statistics.IAPrimitives),
                static_cast<unsigned long long>(statistics.VSInvocations),
                static_cast<unsigned long long>(statistics.PSInvocations),
                static_cast<unsigned long long>(statistics.GSInvocations),
                static_cast<unsigned long long>(statistics.CPrimitives));
    } else {
      PluginLog(false, "[GPU] %s pipeline statistics unavailable hr=%08lX",
                subsystem ? subsystem : "UI3D",
                static_cast<unsigned long>(result));
    }
    ended_ = false;
  }

  ~GpuPipelineProbe() {
    if (query_) query_->Release();
    if (occlusion_query_) occlusion_query_->Release();
  }

 private:
  ID3D11Query* query_ = nullptr;
  ID3D11Query* occlusion_query_ = nullptr;
  bool active_ = false;
  bool ended_ = false;
};

void LogUi3DSceneState(const char* subsystem, void* manager, void* subject) {
  void* camera = manager
                     ? ReadOr<void*>(
                           reinterpret_cast<uint8_t*>(manager) + 0x20,
                           nullptr)
                     : nullptr;
  if (!camera || !subject || !RegionAllows(camera, 0x188) ||
      !RegionAllows(subject, 0x110))
    return;
  SceneTransform camera_local = {};
  SceneTransform camera_world = {};
  SceneTransform subject_world = {};
  float subject_bound[4] = {};
  float frustum[7] = {};
  float port[4] = {};
  const bool local_ok =
      Read(reinterpret_cast<uint8_t*>(camera) + 0x48, &camera_local);
  const bool camera_ok = ReadSceneTransform(camera, &camera_world);
  const bool subject_ok = ReadSceneTransform(subject, &subject_world);
  const bool bound_ok =
      Read(reinterpret_cast<uint8_t*>(subject) + 0xE4, &subject_bound);
  const bool frustum_ok =
      Read(reinterpret_cast<uint8_t*>(camera) + 0x150, &frustum);
  const bool port_ok =
      Read(reinterpret_cast<uint8_t*>(camera) + 0x174, &port);
  PluginLog(false,
            "[GPU] %s scene camera=%p subject=%p parent=%p "
            "camera_local=(%.1f,%.1f,%.1f) "
            "camera_world=(%.1f,%.1f,%.1f) "
            "subject_world=(%.1f,%.1f,%.1f s=%.3f) "
            "bound=(%.1f,%.1f,%.1f r=%.1f) "
            "frustum=(%.3f,%.3f,%.3f,%.3f n=%.2f f=%.1f ortho=%.0f) "
            "port=(%.3f,%.3f,%.3f,%.3f) valid=%u%u%u%u%u",
            subsystem ? subsystem : "UI3D", camera, subject,
            ReadOr<void*>(reinterpret_cast<uint8_t*>(subject) + 0x30,
                          nullptr),
            camera_local.translate[0], camera_local.translate[1],
            camera_local.translate[2], camera_world.translate[0],
            camera_world.translate[1], camera_world.translate[2],
            subject_world.translate[0], subject_world.translate[1],
            subject_world.translate[2], subject_world.scale,
            subject_bound[0], subject_bound[1], subject_bound[2],
            subject_bound[3], frustum[0], frustum[1], frustum[2],
            frustum[3], frustum[4], frustum[5], frustum[6], port[0], port[1],
            port[2], port[3], local_ok ? 1u : 0u, camera_ok ? 1u : 0u,
            subject_ok ? 1u : 0u, bound_ok ? 1u : 0u,
            (frustum_ok && port_ok) ? 1u : 0u);
}

struct NativeCaptureState {
  struct InventoryEntryDataRaw {
    void* object = nullptr;
    void* extra_lists = nullptr;
    int32_t count_delta = 1;
    uint32_t padding = 0;
  };
  static_assert(sizeof(InventoryEntryDataRaw) == 0x18,
                "Skyrim InventoryEntryData layout changed");

  NativeCaptureStage stage = NativeCaptureStage::kIdle;
  NativePreviewRequest request = {};
  InventoryEntryDataRaw entry = {};
  uint64_t started_ms = 0;
  uint64_t last_finished_ms = 0;
  uint64_t capture_started_ms = 0;
  uint64_t next_capture_attempt_ms = 0;
  uint64_t owner_retry_not_before_ms = 0;
  uint64_t unload_started_ms = 0;
  uint64_t last_unload_log_ms = 0;
  unsigned settled_frames = 0;
  unsigned capture_attempts = 0;
  bool unload_called = false;
  bool completion_sent = false;
  bool magic_preview = false;
  bool entry_submitted = false;
  bool unload_reset_requested = false;
  bool manager_lifecycle_owned = false;
  bool manager_ownership_lost = false;
  void* owned_manager = nullptr;
  void* owned_temp_reference = nullptr;
  void* attached_model = nullptr;
  int32_t attached_scheme = -1;
  IDXGISwapChain* resource_chain = nullptr;
  ID3D11Device* resource_device = nullptr;
  ID3D11Texture2D* render_texture = nullptr;
  ID3D11Texture2D* staging = nullptr;
  ID3D11RenderTargetView* render_view = nullptr;
  ID3D11ShaderResourceView* shader_view = nullptr;
  ID3D11Texture2D* depth_texture = nullptr;
  ID3D11DepthStencilView* depth_view = nullptr;
  ID3D11Texture2D* depth_staging = nullptr;
  ID3D11Query* readback_query = nullptr;
  UINT width = 0;
  UINT height = 0;
  float primary_aspect = 1.0f;
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  uint32_t content_samples = 0;
  uint32_t background_samples = 0;
  uint32_t depth_samples = 0;
  uint32_t alpha_samples = 0;
  uint32_t rgb_without_alpha_samples = 0;
  uint8_t minimum_alpha = 255;
  uint8_t maximum_alpha = 0;
  uint8_t minimum_rgb = 255;
  uint8_t maximum_rgb = 0;
  float luminance_stddev = 0.0f;
  uint32_t engine_render_result = 0;
  int32_t rendered_scheme = -1;
  UINT content_min_x = 0;
  UINT content_min_y = 0;
  UINT content_max_x = 0;
  UINT content_max_y = 0;
  float camera_fit_margin = 1.35f;
  float camera_pan_x = 0.0f;
  float camera_pan_z = 0.0f;
  float camera_last_distance = 0.0f;
  float camera_tangent_horizontal = 0.0f;
  float camera_tangent_vertical = 0.0f;
  float camera_expected_width = 0.0f;
  float camera_expected_height = 0.0f;
  uint32_t camera_pan_corrections = 0;
  bool readback_pending = false;
  bool readback_copy_issued = false;
  bool readback_ready = false;
  bool readback_success = false;
  uint64_t readback_dispatched_ms = 0;
  void* logged_load_task = nullptr;
  uint32_t logged_load_state = 0xFFFFFFFFu;
  uint32_t logged_load_phase = 0xFFFFFFFFu;
  uint32_t logged_load_pending = 0xFFFFFFFFu;
};

NativeCaptureState g_native_capture;

void ReleaseNativeCaptureResources() {
  if (g_native_capture.readback_query) {
    g_native_capture.readback_query->Release();
    g_native_capture.readback_query = nullptr;
  }
  if (g_native_capture.depth_staging) {
    g_native_capture.depth_staging->Release();
    g_native_capture.depth_staging = nullptr;
  }
  if (g_native_capture.depth_view) {
    g_native_capture.depth_view->Release();
    g_native_capture.depth_view = nullptr;
  }
  if (g_native_capture.depth_texture) {
    g_native_capture.depth_texture->Release();
    g_native_capture.depth_texture = nullptr;
  }
  if (g_native_capture.shader_view) {
    g_native_capture.shader_view->Release();
    g_native_capture.shader_view = nullptr;
  }
  if (g_native_capture.render_view) {
    g_native_capture.render_view->Release();
    g_native_capture.render_view = nullptr;
  }
  if (g_native_capture.staging) {
    g_native_capture.staging->Release();
    g_native_capture.staging = nullptr;
  }
  if (g_native_capture.render_texture) {
    g_native_capture.render_texture->Release();
    g_native_capture.render_texture = nullptr;
  }
  if (g_native_capture.resource_device) {
    g_native_capture.resource_device->Release();
    g_native_capture.resource_device = nullptr;
  }
  g_native_capture.resource_chain = nullptr;
  g_native_capture.width = 0;
  g_native_capture.height = 0;
  g_native_capture.primary_aspect = 1.0f;
  g_native_capture.format = DXGI_FORMAT_UNKNOWN;
  g_native_capture.readback_pending = false;
  g_native_capture.readback_copy_issued = false;
  g_native_capture.readback_ready = false;
  g_native_capture.readback_success = false;
  g_native_capture.readback_dispatched_ms = 0;
  g_native_capture.camera_fit_margin = 1.35f;
  g_native_capture.camera_pan_x = 0.0f;
  g_native_capture.camera_pan_z = 0.0f;
  g_native_capture.camera_last_distance = 0.0f;
  g_native_capture.camera_tangent_horizontal = 0.0f;
  g_native_capture.camera_tangent_vertical = 0.0f;
  g_native_capture.camera_pan_corrections = 0;
}

int NativePreviewQueueOffset(uint32_t form_id, uint64_t world_epoch) {
  if (!form_id) return -1;
  for (unsigned i = 0; i < g_native_preview_queue_count; ++i) {
    const unsigned at = (g_native_preview_queue_read + i) %
                        kNativePreviewQueueCapacity;
    if (g_native_preview_queue[at].form_id == form_id &&
        g_native_preview_queue[at].world_epoch == world_epoch)
      return static_cast<int>(i);
  }
  return -1;
}

void QueueNativePreview(uint32_t form_id, uint64_t world_epoch,
                        uint32_t owner_deferrals = 0,
                        uint64_t owner_wait_started_ms = 0,
                        bool log_request = true,
                        bool priority = false,
                        bool force_active_requeue = false) {
  if (!form_id) return;
  if (!force_active_requeue &&
      g_native_active_preview_form.load(std::memory_order_acquire) == form_id &&
      g_native_active_preview_epoch.load(std::memory_order_acquire) ==
          world_epoch) {
    return;
  }
  NativePreviewRequest dropped = {};
  unsigned queue_depth = 0;
  bool queued = false;
  bool promoted = false;
  AcquireSRWLockExclusive(&g_native_preview_queue_lock);
  // Normalize the ring into a stable ordered array. This path runs only when
  // the player-update command consumer enqueues a distinct FormID, not per
  // render frame, and makes foreground/background ordering unambiguous.
  for (unsigned i = 0; i < g_native_preview_queue_count; ++i) {
    const unsigned at = (g_native_preview_queue_read + i) %
                        kNativePreviewQueueCapacity;
    g_native_preview_queue_scratch[i] = g_native_preview_queue[at];
  }
  for (auto& request : g_native_preview_queue) request = {};
  for (unsigned i = 0; i < g_native_preview_queue_count; ++i)
    g_native_preview_queue[i] = g_native_preview_queue_scratch[i];
  g_native_preview_queue_read = 0;
  g_native_preview_queue_write =
      g_native_preview_queue_count % kNativePreviewQueueCapacity;
  const int existing_offset =
      NativePreviewQueueOffset(form_id, world_epoch);
  if (existing_offset >= 0) {
    NativePreviewRequest& existing =
        g_native_preview_queue[static_cast<unsigned>(existing_offset)];
    if (priority && !existing.priority) {
      unsigned insert_at = 0;
      while (insert_at < static_cast<unsigned>(existing_offset) &&
             g_native_preview_queue[insert_at].priority)
        ++insert_at;
      NativePreviewRequest promoted_request = existing;
      promoted_request.priority = true;
      // Compute the priority-prefix boundary before changing the existing
      // element. Marking it first made the scan step past itself when it was
      // already the first background request; the subsequent copy duplicated
      // that FormID and silently overwrote the next queued asset.
      for (unsigned at = static_cast<unsigned>(existing_offset);
           at > insert_at; --at)
        g_native_preview_queue[at] = g_native_preview_queue[at - 1];
      g_native_preview_queue[insert_at] = promoted_request;
      promoted = true;
    }
    queue_depth = g_native_preview_queue_count;
  } else {
    bool reject_incoming = false;
    if (g_native_preview_queue_count == kNativePreviewQueueCapacity) {
      // A late background prewarm must never evict a visible request. The
      // capacity covers the complete bridge set, so saturation means a stale
      // or duplicate-generation tail is the safest entry to retire.
      const unsigned drop_at = kNativePreviewQueueCapacity - 1;
      if (!priority && g_native_preview_queue[drop_at].priority) {
        // Every queued request is visible/priority. Reject this late
        // background request instead of silently evicting a selected asset.
        dropped.form_id = form_id;
        dropped.world_epoch = world_epoch;
        reject_incoming = true;
      } else {
        dropped = g_native_preview_queue[drop_at];
        g_native_preview_queue[drop_at] = {};
        --g_native_preview_queue_count;
      }
    }
    if (!reject_incoming) {
      NativePreviewRequest request = {};
      request.form_id = form_id;
      request.owner_deferrals = owner_deferrals;
      request.world_epoch = world_epoch;
      request.owner_wait_started_ms = owner_wait_started_ms;
      request.priority = priority;
      if (priority) {
        unsigned insert_at = 0;
        while (insert_at < g_native_preview_queue_count &&
               g_native_preview_queue[insert_at].priority)
          ++insert_at;
        for (unsigned at = g_native_preview_queue_count; at > insert_at; --at)
          g_native_preview_queue[at] = g_native_preview_queue[at - 1];
        g_native_preview_queue[insert_at] = request;
      } else {
        g_native_preview_queue[g_native_preview_queue_count] = request;
      }
      ++g_native_preview_queue_count;
      g_native_preview_queue_write =
          g_native_preview_queue_count % kNativePreviewQueueCapacity;
      queued = true;
    }
    queue_depth = g_native_preview_queue_count;
  }
  g_native_preview_queue_depth.store(g_native_preview_queue_count,
                                     std::memory_order_release);
  ReleaseSRWLockExclusive(&g_native_preview_queue_lock);
  if (dropped.form_id)
    CompleteNativePreviewFailure(dropped.form_id, dropped.world_epoch,
                                 "request queue full");
  if (queued && log_request)
    PluginLog(false,
              "item-preview: queued form=%08X epoch=%llu depth=%u",
              form_id, static_cast<unsigned long long>(world_epoch),
              queue_depth);
  if (promoted)
    PluginLog(false,
              "item-preview: promoted visible form=%08X epoch=%llu depth=%u",
              form_id, static_cast<unsigned long long>(world_epoch),
              queue_depth);
}

void PurgeQueuedNativePreview(uint32_t form_id, uint64_t world_epoch) {
  if (!form_id) return;
  NativePreviewRequest retained[kNativePreviewQueueCapacity] = {};
  unsigned retained_count = 0;
  AcquireSRWLockExclusive(&g_native_preview_queue_lock);
  for (unsigned i = 0; i < g_native_preview_queue_count; ++i) {
    const unsigned at = (g_native_preview_queue_read + i) %
                        kNativePreviewQueueCapacity;
    const NativePreviewRequest& request = g_native_preview_queue[at];
    if (request.form_id == form_id && request.world_epoch == world_epoch)
      continue;
    retained[retained_count++] = request;
  }
  for (auto& request : g_native_preview_queue) request = {};
  for (unsigned i = 0; i < retained_count; ++i)
    g_native_preview_queue[i] = retained[i];
  g_native_preview_queue_read = 0;
  g_native_preview_queue_count = retained_count;
  g_native_preview_queue_write =
      retained_count % kNativePreviewQueueCapacity;
  g_native_preview_queue_depth.store(retained_count,
                                     std::memory_order_release);
  ReleaseSRWLockExclusive(&g_native_preview_queue_lock);
}

void RequeueNativePreviewForOwner(const NativePreviewRequest& request) {
  QueueNativePreview(request.form_id, request.world_epoch,
                     request.owner_deferrals,
                     request.owner_wait_started_ms, false,
                     request.priority);
}

bool NativePreviewQueueHeadIsPriority() {
  bool priority = false;
  AcquireSRWLockShared(&g_native_preview_queue_lock);
  if (g_native_preview_queue_count)
    priority = g_native_preview_queue[g_native_preview_queue_read].priority;
  ReleaseSRWLockShared(&g_native_preview_queue_lock);
  return priority;
}

bool PopNativePreview(NativePreviewRequest* request) {
  if (!request) return false;
  bool found = false;
  AcquireSRWLockExclusive(&g_native_preview_queue_lock);
  if (g_native_preview_queue_count) {
    *request = g_native_preview_queue[g_native_preview_queue_read];
    g_native_preview_queue[g_native_preview_queue_read] = {};
    g_native_preview_queue_read =
        (g_native_preview_queue_read + 1) % kNativePreviewQueueCapacity;
    --g_native_preview_queue_count;
    g_native_preview_queue_depth.store(g_native_preview_queue_count,
                                       std::memory_order_release);
    found = true;
  }
  ReleaseSRWLockExclusive(&g_native_preview_queue_lock);
  return found;
}

struct LoadedInventoryModelRaw {
  void* item_base;
  void* model_object;
  void* scene_model;
  uint64_t reserved;
};

bool InventoryModels(void* manager, LoadedInventoryModelRaw** models,
                     uint32_t* count) {
  if (!manager || !models || !count || !RegionAllows(manager, 0x160))
    return false;
  auto* array = reinterpret_cast<uint8_t*>(manager) + 0x58;
  const uint32_t traits = ReadOr<uint32_t>(array, 0);
  const uint32_t capacity = traits & 0x7FFFFFFFu;
  const bool local = (traits & 0x80000000u) != 0;
  const uint32_t size = ReadOr<uint32_t>(array + 0xE8, 0);
  if (capacity > 64 || size > capacity || size > 64) return false;
  auto* data = local ? reinterpret_cast<LoadedInventoryModelRaw*>(array + 8)
                     : ReadOr<LoadedInventoryModelRaw*>(array + 8, nullptr);
  if (size && (!data || !RegionAllows(data, sizeof(*data) * size)))
    return false;
  *models = data;
  *count = size;
  return true;
}

struct InventoryLoadTaskSnapshot {
  void* task = nullptr;
  uint32_t state = 0xFFFFFFFFu;
  uint32_t phase = 0xFFFFFFFFu;
  uint32_t pending = 0xFFFFFFFFu;
  void* bound_object = nullptr;
  void* model_object = nullptr;
  void* extra_context = nullptr;
  bool valid = false;
};

InventoryLoadTaskSnapshot ReadInventoryLoadTask(void* manager) {
  InventoryLoadTaskSnapshot snapshot = {};
  if (!manager || !RegionAllows(manager, 0x158)) return snapshot;
  snapshot.task = ReadOr<void*>(
      reinterpret_cast<uint8_t*>(manager) + 0x150, nullptr);
  if (!snapshot.task) {
    snapshot.valid = true;
    return snapshot;
  }
  if (!RegionAllows(snapshot.task, 0x90)) return snapshot;
  auto* task = reinterpret_cast<uint8_t*>(snapshot.task);
  snapshot.state = ReadOr<uint32_t>(task + 0x0C, 0xFFFFFFFFu);
  snapshot.phase = ReadOr<uint32_t>(task + 0x30, 0xFFFFFFFFu);
  snapshot.pending = ReadOr<uint32_t>(task + 0x50, 0xFFFFFFFFu);
  snapshot.bound_object = ReadOr<void*>(task + 0x78, nullptr);
  snapshot.model_object = ReadOr<void*>(task + 0x80, nullptr);
  snapshot.extra_context = ReadOr<void*>(task + 0x88, nullptr);
  snapshot.valid = true;
  return snapshot;
}

void LogInventoryLoadTaskTransition(void* manager, const char* point) {
  const InventoryLoadTaskSnapshot snapshot = ReadInventoryLoadTask(manager);
  if (snapshot.task == g_native_capture.logged_load_task &&
      snapshot.state == g_native_capture.logged_load_state &&
      snapshot.phase == g_native_capture.logged_load_phase &&
      snapshot.pending == g_native_capture.logged_load_pending)
    return;
  g_native_capture.logged_load_task = snapshot.task;
  g_native_capture.logged_load_state = snapshot.state;
  g_native_capture.logged_load_phase = snapshot.phase;
  g_native_capture.logged_load_pending = snapshot.pending;
  PluginLog(false,
            "[ITEM_LOAD] form=%08X point=%s task=%p valid=%u state=%u "
            "phase=%u pending=%u bound=%p model=%p context=%p models=%u",
            g_native_capture.request.form_id, point ? point : "unknown",
            snapshot.task, snapshot.valid ? 1u : 0u, snapshot.state,
            snapshot.phase, snapshot.pending, snapshot.bound_object,
            snapshot.model_object, snapshot.extra_context,
            ReadOr<uint32_t>(reinterpret_cast<uint8_t*>(manager) + 0x140, 0));
}

bool InventoryLoadInFlight(void* manager) {
  if (!manager || !RegionAllows(manager, 0x158)) return true;
  if (ReadOr<void*>(reinterpret_cast<uint8_t*>(manager) + 0x150,
                    nullptr)) return true;
  LoadedInventoryModelRaw* models = nullptr;
  uint32_t count = 0;
  if (!InventoryModels(manager, &models, &count)) return true;
  for (uint32_t i = 0; i < count; ++i)
    if (!models[i].scene_model) return true;
  return false;
}

void* FindInventorySceneModel(void* manager, void* bound_object) {
  LoadedInventoryModelRaw* models = nullptr;
  uint32_t count = 0;
  if (!InventoryModels(manager, &models, &count)) return nullptr;
  for (uint32_t i = 0; i < count; ++i) {
    if (models[i].scene_model &&
        (models[i].item_base == bound_object ||
         models[i].model_object == bound_object)) {
      return models[i].scene_model;
    }
  }
  // Never capture another menu's most-recent model as a fallback.  A native
  // preview is valid only when Skyrim associated it with this exact object.
  return nullptr;
}

int32_t InventorySceneScheme(void* scene_model) {
  void* scene_manager = ReadOr<void*>(
      reinterpret_cast<void*>(g_base + kUI3DSceneManagerPtr), nullptr);
  if (!scene_model || !scene_manager || !RegionAllows(scene_manager, 0x118))
    return -1;
  void* object = scene_model;
  for (unsigned depth = 0; object && depth < 24; ++depth) {
    for (int32_t scheme = 0; scheme < 8; ++scheme) {
      void* root = ReadOr<void*>(
          reinterpret_cast<uint8_t*>(scene_manager) + 0x38 +
              static_cast<size_t>(scheme) * sizeof(void*),
          nullptr);
      if (root && object == root) return scheme;
    }
    object = ReadOr<void*>(reinterpret_cast<uint8_t*>(object) + 0x30,
                           nullptr);
  }
  return -1;
}

bool EnsureNativeCaptureResources(IDXGISwapChain* chain,
                                  ID3D11Device* device,
                                  ID3D11Texture2D* back_buffer,
                                  const D3D11_TEXTURE2D_DESC& source) {
  const bool format_ok =
      source.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
      source.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
      source.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
      source.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
  if (!chain || !device || !back_buffer || !format_ok ||
      source.SampleDesc.Count != 1 || source.Width < 320 ||
      source.Height < 240) return false;
  const float primary_aspect = static_cast<float>(source.Width) /
                               static_cast<float>(source.Height);
  if (!std::isfinite(primary_aspect) || primary_aspect < 0.5f ||
      primary_aspect > 4.0f)
    return false;
  // Skyrim's Inventory3DManager composes its menu model in the native 512px
  // UI3D coordinate space before our viewport hook takes ownership.  Reducing
  // the private target to 256px did not merely lower resolution: real raster
  // bounds from otherwise-valid models remained in the 256..511 half of that
  // coordinate space, so color and depth were clipped even though geometry
  // reached the accumulator.  Keep Skyrim's native working extent here and
  // downsample only the completed result to the bridge's 192px cache.
  constexpr UINT kCaptureWidth = 512;
  constexpr UINT kCaptureHeight = 512;
  if (g_native_capture.resource_chain == chain &&
      g_native_capture.resource_device == device &&
      g_native_capture.render_texture && g_native_capture.staging &&
      g_native_capture.render_view && g_native_capture.shader_view &&
      g_native_capture.depth_texture && g_native_capture.depth_view &&
      g_native_capture.depth_staging && g_native_capture.readback_query &&
      g_native_capture.width == kCaptureWidth &&
      g_native_capture.height == kCaptureHeight &&
      std::abs(g_native_capture.primary_aspect - primary_aspect) < 0.0001f &&
      g_native_capture.format == source.Format) return true;

  ReleaseNativeCaptureResources();
  D3D11_TEXTURE2D_DESC render_desc = {};
  render_desc.Width = kCaptureWidth;
  render_desc.Height = kCaptureHeight;
  render_desc.MipLevels = 1;
  render_desc.ArraySize = 1;
  render_desc.Format = source.Format;
  render_desc.SampleDesc.Count = 1;
  render_desc.Usage = D3D11_USAGE_DEFAULT;
  render_desc.BindFlags =
      D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  if (FAILED(device->CreateTexture2D(&render_desc, nullptr,
                                     &g_native_capture.render_texture)) ||
      FAILED(device->CreateRenderTargetView(
          g_native_capture.render_texture, nullptr,
          &g_native_capture.render_view)) ||
      FAILED(device->CreateShaderResourceView(
          g_native_capture.render_texture, nullptr,
          &g_native_capture.shader_view))) {
    ReleaseNativeCaptureResources();
    return false;
  }
  D3D11_TEXTURE2D_DESC staging_desc = render_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  // Keep the item renderer's native depth test, but use a typeless D32
  // resource so the exact pixels that Skyrim submitted can also be copied to
  // a CPU-readable staging texture.  Color-keying alone is not a coverage
  // signal: several UI3D lighting paths write a non-magenta value across the
  // entire color target even though model geometry covers only a small part.
  D3D11_TEXTURE2D_DESC depth_desc = {};
  depth_desc.Width = kCaptureWidth;
  depth_desc.Height = kCaptureHeight;
  depth_desc.MipLevels = 1;
  depth_desc.ArraySize = 1;
  depth_desc.Format = DXGI_FORMAT_R32_TYPELESS;
  depth_desc.SampleDesc.Count = 1;
  depth_desc.Usage = D3D11_USAGE_DEFAULT;
  depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
  D3D11_DEPTH_STENCIL_VIEW_DESC depth_view_desc = {};
  depth_view_desc.Format = DXGI_FORMAT_D32_FLOAT;
  depth_view_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
  depth_view_desc.Texture2D.MipSlice = 0;
  D3D11_TEXTURE2D_DESC depth_staging_desc = depth_desc;
  depth_staging_desc.Usage = D3D11_USAGE_STAGING;
  depth_staging_desc.BindFlags = 0;
  depth_staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  if (FAILED(device->CreateTexture2D(&staging_desc, nullptr,
                                     &g_native_capture.staging)) ||
      FAILED(device->CreateTexture2D(&depth_desc, nullptr,
                                     &g_native_capture.depth_texture)) ||
      FAILED(device->CreateDepthStencilView(
          g_native_capture.depth_texture, &depth_view_desc,
          &g_native_capture.depth_view)) ||
      FAILED(device->CreateTexture2D(&depth_staging_desc, nullptr,
                                     &g_native_capture.depth_staging))) {
    PluginLog(false,
              "item-preview: off-screen color/depth resource creation failed");
    ReleaseNativeCaptureResources();
    return false;
  }
  D3D11_QUERY_DESC query_desc = {};
  query_desc.Query = D3D11_QUERY_EVENT;
  const HRESULT query_result =
      device->CreateQuery(&query_desc, &g_native_capture.readback_query);
  if (FAILED(query_result) || !g_native_capture.readback_query) {
    PluginLog(false,
              "item-preview: readback event creation failed hr=%08lX",
              static_cast<unsigned long>(query_result));
    ReleaseNativeCaptureResources();
    return false;
  }
  device->AddRef();
  g_native_capture.resource_device = device;
  g_native_capture.resource_chain = chain;
  g_native_capture.width = kCaptureWidth;
  g_native_capture.height = kCaptureHeight;
  g_native_capture.primary_aspect = primary_aspect;
  g_native_capture.format = source.Format;
  PluginLog(false,
            "item-preview: isolated off-screen surface ready %ux%u format=%u "
            "primary=%ux%u aspect=%.6f",
            kCaptureWidth, kCaptureHeight,
            static_cast<unsigned>(source.Format), source.Width, source.Height,
            primary_aspect);
  return true;
}

struct VisibleGeometryBounds {
  float minimum[3] = {};
  float maximum[3] = {};
  uint32_t visited = 0;
  uint32_t bounded = 0;
  uint32_t geometry = 0;
  uint32_t renderable_geometry = 0;
  uint32_t vertex_geometries = 0;
  uint32_t sampled_vertices = 0;
  bool valid = false;
};

size_t ReadableRegionBytes(const void* address);
uint32_t VertexStride(uint64_t descriptor);

// NiNode bounds are often deliberately broad, and the root bound used by the
// inventory loader can still describe the pre-Inventory3DManager transform.
// Walk the actual BSGeometry children and aggregate their current world-space
// bounds.  This is the same information Skyrim sends to its culling process,
// so fitting this volume guarantees that the submitted meshes, not a guessed
// form radius, determine the camera.
bool MeasureVisibleGeometry(void* root, VisibleGeometryBounds* result,
                            float maximum_radius,
                            float maximum_center_offset,
                            bool measure_static_vertices = false) {
  if (!root || !result || maximum_radius <= 0.0f ||
      maximum_center_offset <= 0.0f)
    return false;
  *result = {};
  constexpr uint32_t kCapacity = 1024;
  void* queue[kCapacity] = {};
  uint32_t read_index = 0;
  uint32_t queue_count = 1;
  queue[0] = root;

  SceneTransform root_world = {};
  if (!ReadSceneTransform(root, &root_world)) return false;
  const float root_x = root_world.translate[0];
  const float root_y = root_world.translate[1];
  const float root_z = root_world.translate[2];

  auto include_point = [&](float x, float y, float z) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        std::abs(x - root_x) > maximum_center_offset ||
        std::abs(y - root_y) > maximum_center_offset ||
        std::abs(z - root_z) > maximum_center_offset)
      return false;
    const float point[3] = {x, y, z};
    const bool had_value = result->valid;
    for (unsigned axis = 0; axis < 3; ++axis) {
      if (!had_value) {
        result->minimum[axis] = point[axis];
        result->maximum[axis] = point[axis];
      } else {
        result->minimum[axis] =
            std::min(result->minimum[axis], point[axis]);
        result->maximum[axis] =
            std::max(result->maximum[axis], point[axis]);
      }
    }
    result->valid = true;
    return true;
  };

  auto include_static_vertices = [&](void* geometry) {
    if (!measure_static_vertices || !geometry ||
        !RegionAllows(geometry, 0x15C) ||
        // Skinned actor vertices are in bind space and need bone matrices.
        // Inventory ground models are unskinned, so their loaded vertex data
        // can be transformed exactly by the geometry's current world matrix.
        ReadOr<void*>(reinterpret_cast<uint8_t*>(geometry) + 0x130,
                      nullptr))
      return false;
    void* renderer = ReadOr<void*>(
        reinterpret_cast<uint8_t*>(geometry) + 0x138, nullptr);
    uint8_t* raw = renderer
        ? ReadOr<uint8_t*>(reinterpret_cast<uint8_t*>(renderer) + 0x20,
                           nullptr)
        : nullptr;
    uint64_t descriptor = ReadOr<uint64_t>(
        reinterpret_cast<uint8_t*>(geometry) + 0x148, 0);
    if (!descriptor && renderer)
      descriptor = ReadOr<uint64_t>(
          reinterpret_cast<uint8_t*>(renderer) + 0x10, 0);
    const uint32_t stride = VertexStride(descriptor);
    uint32_t vertices = ReadOr<uint16_t>(
        reinterpret_cast<uint8_t*>(geometry) + 0x15A, 0);
    SceneTransform world = {};
    if (!raw || !stride || !vertices || !ReadSceneTransform(geometry, &world))
      return false;
    const size_t readable = ReadableRegionBytes(raw);
    vertices = std::min<uint32_t>(
        vertices, static_cast<uint32_t>(
                      std::min<size_t>(readable / stride, 65535)));
    if (!vertices) return false;

    uint32_t accepted = 0;
    for (uint32_t vertex = 0; vertex < vertices; ++vertex) {
      float local[3] = {};
      std::memcpy(local, raw + static_cast<size_t>(vertex) * stride,
                  sizeof(local));
      if (!std::isfinite(local[0]) || !std::isfinite(local[1]) ||
          !std::isfinite(local[2]) || std::abs(local[0]) > 100000.0f ||
          std::abs(local[1]) > 100000.0f ||
          std::abs(local[2]) > 100000.0f)
        continue;
      const float scaled_x = local[0] * world.scale;
      const float scaled_y = local[1] * world.scale;
      const float scaled_z = local[2] * world.scale;
      const float x = world.rotate[0] * scaled_x +
                      world.rotate[1] * scaled_y +
                      world.rotate[2] * scaled_z + world.translate[0];
      const float y = world.rotate[3] * scaled_x +
                      world.rotate[4] * scaled_y +
                      world.rotate[5] * scaled_z + world.translate[1];
      const float z = world.rotate[6] * scaled_x +
                      world.rotate[7] * scaled_y +
                      world.rotate[8] * scaled_z + world.translate[2];
      if (include_point(x, y, z)) ++accepted;
    }
    if (!accepted) return false;
    ++result->vertex_geometries;
    result->sampled_vertices += accepted;
    return true;
  };

  auto include_bound = [&](void* object) {
    float bound[4] = {};
    if (!object || !Read(reinterpret_cast<uint8_t*>(object) + 0xE4, &bound) ||
        !std::isfinite(bound[0]) || !std::isfinite(bound[1]) ||
        !std::isfinite(bound[2]) || !std::isfinite(bound[3]) ||
        bound[3] < 0.01f || bound[3] > maximum_radius ||
        std::abs(bound[0] - root_x) > maximum_center_offset ||
        std::abs(bound[1] - root_y) > maximum_center_offset ||
        std::abs(bound[2] - root_z) > maximum_center_offset)
      return;
    const bool had_bound = result->valid;
    for (unsigned axis = 0; axis < 3; ++axis) {
      const float low = bound[axis] - bound[3];
      const float high = bound[axis] + bound[3];
      if (!had_bound) {
        result->minimum[axis] = low;
        result->maximum[axis] = high;
      } else {
        result->minimum[axis] = std::min(result->minimum[axis], low);
        result->maximum[axis] = std::max(result->maximum[axis], high);
      }
    }
    result->valid = true;
    ++result->bounded;
  };

  auto visible_from_root = [&](void* object) {
    for (uint32_t depth = 0; object && depth < 64; ++depth) {
      if (!RegionAllows(object, 0xF8) ||
          (ReadOr<uint32_t>(reinterpret_cast<uint8_t*>(object) + 0xF4,
                            1u) &
           1u))
        return false;
      if (object == root) return true;
      object = ReadOr<void*>(reinterpret_cast<uint8_t*>(object) + 0x30,
                             nullptr);
    }
    return false;
  };

  while (read_index < queue_count && read_index < kCapacity) {
    void* object = queue[read_index++];
    if (!object || !RegionAllows(object, 0x110)) continue;
    ++result->visited;
    if (void* geometry = SceneVirtualPointer(object, 7)) {
      ++result->geometry;
      if (ReadOr<void*>(reinterpret_cast<uint8_t*>(geometry) + 0x138,
                        nullptr))
        ++result->renderable_geometry;
      // Hidden equipment variants, collision helpers, and effect geometry can
      // carry large valid bounds while contributing no pixels. Including them
      // makes the real model microscopic and causes camera motion as Skyrim
      // toggles visibility. Match culling visibility through the parent chain.
      if (visible_from_root(geometry) &&
          !include_static_vertices(geometry))
        include_bound(geometry);
    }
    if (void* node = SceneVirtualPointer(object, 3)) {
      void** children = ReadOr<void**>(
          reinterpret_cast<uint8_t*>(node) + 0x118, nullptr);
      uint16_t capacity = ReadOr<uint16_t>(
          reinterpret_cast<uint8_t*>(node) + 0x120, 0);
      capacity = std::min<uint16_t>(capacity, 512);
      for (uint16_t child_index = 0;
           children && child_index < capacity && queue_count < kCapacity;
           ++child_index) {
        void* child = ReadOr<void*>(children + child_index, nullptr);
        if (!child || !RegionAllows(child, sizeof(void*))) continue;
        bool duplicate = false;
        for (uint32_t prior = 0; prior < queue_count; ++prior) {
          if (queue[prior] == child) {
            duplicate = true;
            break;
          }
        }
        if (!duplicate) queue[queue_count++] = child;
      }
    }
  }
  // Preserve a safe fallback for unusual NIFs whose visible partition stores
  // its bound on an intermediate AVObject.
  if (!result->valid) include_bound(root);
  return result->valid;
}

bool ComputePerspectiveFit(const VisibleGeometryBounds& bounds,
                           uint32_t width, uint32_t height,
                           float tangent_horizontal, float tangent_vertical,
                           float margin,
                           float zoom, float minimum_distance,
                           float* target, float* position,
                           float* distance_out) {
  if (!bounds.valid || !target || !position || !width || !height ||
      !std::isfinite(tangent_horizontal) ||
      !std::isfinite(tangent_vertical) || tangent_horizontal <= 0.0001f ||
      tangent_vertical <= 0.0001f)
    return false;

  for (unsigned axis = 0; axis < 3; ++axis) {
    if (!std::isfinite(bounds.minimum[axis]) ||
        !std::isfinite(bounds.maximum[axis]) ||
        bounds.maximum[axis] < bounds.minimum[axis])
      return false;
    target[axis] =
        (bounds.minimum[axis] + bounds.maximum[axis]) * 0.5f;
  }
  const float half_width =
      std::max(0.5f, (bounds.maximum[0] - bounds.minimum[0]) * 0.5f);
  const float half_depth =
      std::max(0.5f, (bounds.maximum[1] - bounds.minimum[1]) * 0.5f);
  const float half_height =
      std::max(0.5f, (bounds.maximum[2] - bounds.minimum[2]) * 0.5f);
  const float safe_margin = std::clamp(margin, 1.05f, 6.0f);
  const float safe_zoom = std::clamp(zoom, 0.25f, 4.0f);
  // The camera is placed on +Y and looks back toward the model. Account for
  // the part of the volume nearest the camera before applying the screen-space
  // fit; otherwise deep meshes can cross the near side of the fitted cone.
  const float projected_distance = std::max(
      half_width / tangent_horizontal, half_height / tangent_vertical);
  const float distance = std::clamp(
      half_depth + projected_distance * safe_margin / safe_zoom,
      minimum_distance, 250000.0f);
  position[0] = target[0];
  position[1] = target[1] + distance;
  position[2] = target[2];
  if (distance_out) *distance_out = distance;
  return std::isfinite(distance);
}

bool ReadCameraPerspectiveTangents(void* camera, float* horizontal,
                                   float* vertical) {
  if (!camera || !horizontal || !vertical ||
      !RegionAllows(reinterpret_cast<uint8_t*>(camera) + 0x150,
                    sizeof(float) * 7))
    return false;
  float frustum[7] = {};
  if (!Read(reinterpret_cast<uint8_t*>(camera) + 0x150, &frustum))
    return false;
  // NiCamera stores the perspective slopes produced by Skyrim's own UI3D
  // SetCameraFOV helper. Its public-looking parameter is not a conventional
  // vertical degree FOV: 1.5.97 multiplies it by 0.15 and another 0.75 before
  // building this frustum. Reading the committed frustum avoids the 3.6-4.6x
  // under-distance caused by applying tan(fov/2) a second, incompatible way.
  const float tangent_horizontal =
      std::max(std::abs(frustum[0]), std::abs(frustum[1]));
  const float tangent_vertical =
      std::max(std::abs(frustum[2]), std::abs(frustum[3]));
  if (!std::isfinite(tangent_horizontal) ||
      !std::isfinite(tangent_vertical) || tangent_horizontal <= 0.0001f ||
      tangent_vertical <= 0.0001f || tangent_horizontal > 10.0f ||
      tangent_vertical > 10.0f || frustum[6] != 0.0f)
    return false;
  *horizontal = tangent_horizontal;
  *vertical = tangent_vertical;
  return true;
}

// UI3DSceneManager owns one process-global NiCamera. Its public Save/Restore
// helpers mutate that object and reconstruct its derived matrices on restore;
// doing that from HUD PostDisplay made the live gameplay view flash whenever a
// burst of inventory previews was serviced. NiCamera is 0x188 bytes in Skyrim
// SE 1.5.97 and is safe to snapshot for one synchronous UI3D submission: the
// render path only reads the inherited camera object after the normal camera
// helpers have updated its transform/frustum. The manager pointer is restored
// before control returns to Skyrim, so the private copy never owns or releases
// any of the shallow-copied NiAVObject references.
class ScopedPrivateUi3DCamera {
 public:
  ScopedPrivateUi3DCamera() = default;
  ScopedPrivateUi3DCamera(const ScopedPrivateUi3DCamera&) = delete;
  ScopedPrivateUi3DCamera& operator=(const ScopedPrivateUi3DCamera&) = delete;

  ~ScopedPrivateUi3DCamera() { Restore(); }

  bool Acquire(void* manager, void* source_camera = nullptr) {
    if (active_ || !manager || !RegionWritable(
            reinterpret_cast<uint8_t*>(manager) + 0x20, sizeof(void*)))
      return false;
    original_ = ReadOr<void*>(reinterpret_cast<uint8_t*>(manager) + 0x20,
                              nullptr);
    if (!original_ || !RegionAllows(original_, kNiCameraSize)) return false;
    void* const source = source_camera ? source_camera : original_;
    if (!RegionAllows(source, kNiCameraSize)) return false;
    std::memcpy(storage_, source, kNiCameraSize);
    // The copy is deliberately non-owning. Keep its intrusive count far from
    // zero in case an internal temporary NiPointer briefly observes it.
    const LONG pinned_reference_count = 0x100000;
    std::memcpy(storage_ + 0x08, &pinned_reference_count,
                sizeof(pinned_reference_count));
    manager_ = manager;
    void* camera = storage_;
    if (!WriteValue(reinterpret_cast<uint8_t*>(manager_) + 0x20, camera)) {
      manager_ = nullptr;
      original_ = nullptr;
      return false;
    }
    active_ = true;
    return true;
  }

  bool Restore() {
    if (!active_) return true;
    bool restored = false;
    if (manager_ && RegionWritable(
            reinterpret_cast<uint8_t*>(manager_) + 0x20, sizeof(void*))) {
      restored = WriteValue(reinterpret_cast<uint8_t*>(manager_) + 0x20,
                            original_);
    }
    if (!restored) {
      PluginLog(true,
                "[CAMERA_ISOLATION] failed to restore UI3D camera pointer "
                "manager=%p original=%p private=%p",
                manager_, original_, storage_);
      g_native_render_reset_revision.fetch_add(1,
                                                std::memory_order_acq_rel);
    }
    active_ = false;
    manager_ = nullptr;
    original_ = nullptr;
    return restored;
  }

  void* camera() const {
    return active_ ? const_cast<uint8_t*>(storage_) : nullptr;
  }

 private:
  static constexpr size_t kNiCameraSize = 0x188;
  void* manager_ = nullptr;
  void* original_ = nullptr;
  bool active_ = false;
  alignas(16) uint8_t storage_[kNiCameraSize] = {};
};

// Skyrim's deferred skinned UI3D submission is tied to the manager-owned
// NiCamera. Static inventory geometry renders correctly through a private
// camera copy, but the live actor produced an empty depth buffer whenever that
// copy was used. Keep the private path for items and use Skyrim's native
// Save/Restore pair only around the character submission. The camera is
// restored before returning to the world renderer; only the off-screen
// framebuffer lease remains armed until Present drains the actor draw.
bool SaveSharedCharacterCamera(void* manager) {
  using SaveFn = void(*)(void*);
  auto save = reinterpret_cast<SaveFn>(g_base + kUI3DSaveCamera);
  if (!manager ||
      !RegionAllows(reinterpret_cast<void*>(save), 1, true))
    return false;
  save(manager);
  return true;
}

bool RestoreSharedCharacterCamera(void* manager) {
  using RestoreFn = void(*)(void*);
  auto restore = reinterpret_cast<RestoreFn>(g_base + kUI3DRestoreCamera);
  if (!manager ||
      !RegionAllows(reinterpret_cast<void*>(restore), 1, true))
    return false;
  restore(manager);
  return true;
}

bool SetPrivateUi3DCameraFov(void* manager, void* camera,
                             float fov_parameter, uint32_t width,
                             uint32_t height, float overscan_scale,
                             float horizontal_aspect_compensation,
                             float* tangent_horizontal,
                             float* tangent_vertical) {
  if (!manager || !camera || !width || !height ||
      !std::isfinite(overscan_scale) || overscan_scale <= 0.0f ||
      !std::isfinite(horizontal_aspect_compensation) ||
      horizontal_aspect_compensation < 0.5f ||
      horizontal_aspect_compensation > 4.0f ||
      !tangent_horizontal || !tangent_vertical ||
      !RegionWritable(reinterpret_cast<uint8_t*>(camera) + 0x150, 0x24))
    return false;
  using SetFovFn = void(*)(void*, float);
  using SetFrustumFn = void(*)(void*, const float*);
  auto set_fov = reinterpret_cast<SetFovFn>(g_base + kUI3DSetCameraFov);
  auto set_frustum = reinterpret_cast<SetFrustumFn>(g_base + 0x00C651D0);
  if (!RegionAllows(reinterpret_cast<void*>(set_fov), 1, true) ||
      !RegionAllows(reinterpret_cast<void*>(set_frustum), 1, true))
    return false;

  // SetFOV establishes Skyrim's native near/far planes and horizontal slope
  // on the private camera. Replace only its aspect-dependent vertical slope;
  // this reproduces the helper's exact 1.5.97 math without ever publishing the
  // portrait dimensions through the process-wide screen globals.
  set_fov(manager, fov_parameter);
  float frustum[7] = {};
  if (!Read(reinterpret_cast<uint8_t*>(camera) + 0x150, &frustum))
    return false;
  const float native_horizontal =
      std::max(std::abs(frustum[0]), std::abs(frustum[1]));
  if (!std::isfinite(native_horizontal) || native_horizontal <= 0.0001f)
    return false;
  // UI3D's final menu transform still contains the primary swap-chain aspect.
  // On the live 1920x1080 game this expanded horizontal depth silhouettes by
  // 1.78 even on our square target (confirmed independently by a hood and a
  // sword). Widen only the camera's horizontal slope by that source aspect so
  // the post-transform cancels it; vertical framing stays tied to the actual
  // private target dimensions.
  const float horizontal = native_horizontal * overscan_scale *
                           horizontal_aspect_compensation;
  const float vertical = native_horizontal * static_cast<float>(height) /
                         static_cast<float>(width) * overscan_scale;
  if (!std::isfinite(horizontal) || !std::isfinite(vertical) ||
      horizontal <= 0.0001f || vertical <= 0.0001f ||
      horizontal > 10.0f || vertical > 10.0f)
    return false;
  frustum[0] = -horizontal;
  frustum[1] = horizontal;
  frustum[2] = vertical;
  frustum[3] = -vertical;
  frustum[6] = 0.0f;
  set_frustum(camera, frustum);
  // NiCamera+0x170 is the maximum far/near ratio, not an aspect field. The
  // native SetFOV call above has already populated it; leave it untouched.
  return ReadCameraPerspectiveTangents(camera, tangent_horizontal,
                                       tangent_vertical);
}

bool ApplySilhouetteCameraCorrection(
    const char* owner, uint32_t width, uint32_t height, uint32_t min_x,
    uint32_t min_y, uint32_t max_x, uint32_t max_y, bool clipped,
    float distance, float tangent_horizontal, float tangent_vertical,
    float* pan_x, float* pan_z, uint32_t* correction_count) {
  if (!owner || !width || !height || min_x > max_x || min_y > max_y ||
      !pan_x || !pan_z || !correction_count || !std::isfinite(distance) ||
      distance <= 0.0f || !std::isfinite(tangent_horizontal) ||
      !std::isfinite(tangent_vertical) || tangent_horizontal <= 0.0f ||
      tangent_vertical <= 0.0f)
    return false;
  const float desired_x = (static_cast<float>(width) - 1.0f) * 0.5f;
  const float desired_y = (static_cast<float>(height) - 1.0f) * 0.5f;
  const float observed_x =
      (static_cast<float>(min_x) + static_cast<float>(max_x)) * 0.5f;
  const float observed_y =
      (static_cast<float>(min_y) + static_cast<float>(max_y)) * 0.5f;
  float error_x = observed_x - desired_x;
  float error_y = observed_y - desired_y;
  constexpr uint32_t kUnobservableEdgeGuard = 4;
  if (min_x <= kUnobservableEdgeGuard &&
      max_x + kUnobservableEdgeGuard >= width - 1)
    error_x = 0.0f;
  if (min_y <= kUnobservableEdgeGuard &&
      max_y + kUnobservableEdgeGuard >= height - 1)
    error_y = 0.0f;
  const float tolerance_x = std::max(3.0f, width * 0.025f);
  const float tolerance_y = std::max(3.0f, height * 0.025f);
  if (std::abs(error_x) <= tolerance_x &&
      std::abs(error_y) <= tolerance_y)
    return false;

  // The fixed UI3D camera looks along world -Y. Its screen-right basis is
  // world -X and screen-up is world +Z (verified from the live 1.5.97 camera
  // matrix). Move camera and target together so the view basis cannot rotate.
  // A clipped bbox underestimates the hidden half of the silhouette, so use a
  // damped, bounded correction and refine it from the next real depth frame.
  // Live depth traces show the UI3D submission responds more strongly than
  // the ideal pinhole estimate once its menu transform is applied. Keep this
  // feedback intentionally under-damped; complete item silhouettes are
  // cropped/centered on the CPU and actor centering converges over a few
  // static passes without alternating across the target.
  // Live 1.5.97 traces show that the native inventory/menu transform adds a
  // large post-scene offset which is absent from NiAVObject world bounds. A
  // 0.30 gain needed five to eight real GPU readbacks per object. Use the
  // authoritative depth silhouette to correct most of that error in one
  // bounded step; a following frame still verifies the complete mesh.
  const bool item_preview = std::strcmp(owner, "ITEM_RENDER") == 0;
  const float gain = clipped ? (item_preview ? 0.65f : 0.90f) : 0.30f;
  const float half_span_x = distance * tangent_horizontal;
  const float half_span_z = distance * tangent_vertical;
  float delta_x = -gain * (2.0f * error_x / width) * half_span_x;
  float delta_z = -gain * (2.0f * error_y / height) * half_span_z;
  const float maximum_step = clipped
      ? (item_preview ? 0.38f : 0.55f)
      : 0.24f;
  delta_x = std::clamp(delta_x, -half_span_x * maximum_step,
                       half_span_x * maximum_step);
  delta_z = std::clamp(delta_z, -half_span_z * maximum_step,
                       half_span_z * maximum_step);
  const float old_x = *pan_x;
  const float old_z = *pan_z;
  *pan_x = std::clamp(old_x + delta_x, -half_span_x * 4.0f,
                      half_span_x * 4.0f);
  *pan_z = std::clamp(old_z + delta_z, -half_span_z * 4.0f,
                      half_span_z * 4.0f);
  ++*correction_count;
  PluginLog(false,
            "[CAMERA_FEEDBACK] owner=%s bounds=%u,%u..%u,%u "
            "error=(%.1f,%.1f) pan=(%.2f,%.2f)->(%.2f,%.2f) "
            "distance=%.1f slopes=(%.6f,%.6f) clipped=%u step=%u",
            owner, min_x, min_y, max_x, max_y, error_x, error_y, old_x,
            old_z, *pan_x, *pan_z, distance, tangent_horizontal,
            tangent_vertical, clipped ? 1u : 0u, *correction_count);
  return true;
}

bool ConvertNativeCapture(const D3D11_MAPPED_SUBRESOURCE& mapped,
                          const D3D11_MAPPED_SUBRESOURCE& mapped_depth,
                          UINT width, UINT height, DXGI_FORMAT format) {
  if (!mapped.pData || mapped.RowPitch < width * 4 ||
      !mapped_depth.pData || mapped_depth.RowPitch < width * sizeof(float))
    return false;
  const bool bgra = format == DXGI_FORMAT_B8G8R8A8_UNORM ||
                    format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
  auto rgba_at = [&](UINT x, UINT y, uint8_t* rgba) {
    const auto* pixel = static_cast<const uint8_t*>(mapped.pData) +
                        static_cast<size_t>(y) * mapped.RowPitch + x * 4;
    rgba[0] = bgra ? pixel[2] : pixel[0];
    rgba[1] = pixel[1];
    rgba[2] = bgra ? pixel[0] : pixel[2];
    rgba[3] = pixel[3];
  };
  auto has_written_depth = [&](UINT x, UINT y) {
    float depth = 1.0f;
    const auto* source = static_cast<const uint8_t*>(mapped_depth.pData) +
                         static_cast<size_t>(y) * mapped_depth.RowPitch +
                         x * sizeof(float);
    std::memcpy(&depth, source, sizeof(depth));
    // The private DSV is cleared to exactly 1.0 before Skyrim renders. Any
    // finite value below that is coverage generated by Skyrim's own depth
    // test, independent of what its lighting shaders wrote outside the mesh.
    return std::isfinite(depth) && depth >= 0.0f && depth < 1.0f;
  };

  uint32_t depth_samples = 0;
  for (UINT y = 0; y < height; ++y) {
    for (UINT x = 0; x < width; ++x) {
      if (has_written_depth(x, y)) ++depth_samples;
    }
  }
  g_native_capture.depth_samples = depth_samples;
  // Opaque Skyrim inventory geometry writes depth. A tiny threshold avoids
  // treating uninitialized/denormal staging noise as a silhouette while still
  // allowing very thin arrows, lockpicks, and weapon tips. Skyrim magic art is
  // intentionally rendered by transparent no-depth BSEffectShader passes, so
  // those frames use the render target's real alpha/colour coverage instead.
  const bool use_depth_coverage = depth_samples >= 16;

  // UI3D's transparent-effect pass can replace the target alpha with 255 over
  // the complete viewport even though its clear/background RGB remains black.
  // Treating absolute alpha as coverage therefore turns every spell into an
  // opaque 512x512 rectangle. Derive the real clear value from the darkest
  // corner and classify no-depth particles by their difference from that
  // background. This works for conventional and additive Skyrim effects and
  // does not depend on the render pass preserving our original clear alpha.
  uint8_t background_rgba[4] = {};
  uint32_t darkest_corner_energy = UINT32_MAX;
  const UINT corner_x[4] = {0, width - 1, 0, width - 1};
  const UINT corner_y[4] = {0, 0, height - 1, height - 1};
  for (unsigned corner = 0; corner < 4; ++corner) {
    uint8_t sample[4] = {};
    rgba_at(corner_x[corner], corner_y[corner], sample);
    const uint32_t energy = static_cast<uint32_t>(sample[0]) + sample[1] +
                            sample[2];
    if (energy < darkest_corner_energy) {
      darkest_corner_energy = energy;
      std::memcpy(background_rgba, sample, sizeof(background_rgba));
    }
  }
  auto effect_coverage = [&](const uint8_t* rgba) {
    unsigned difference = 0;
    for (unsigned channel = 0; channel < 4; ++channel) {
      difference = std::max(
          difference,
          static_cast<unsigned>(std::abs(
              static_cast<int>(rgba[channel]) -
              static_cast<int>(background_rgba[channel]))));
    }
    return static_cast<uint8_t>(std::min(difference, 255u));
  };

  UINT min_x = width, min_y = height, max_x = 0, max_y = 0;
  bool found = false;
  uint32_t background_samples = 0;
  uint32_t content_samples = 0;
  uint8_t minimum_rgb = 255;
  uint8_t maximum_rgb = 0;
  uint8_t minimum_alpha = 255;
  uint8_t maximum_alpha = 0;
  uint32_t alpha_samples = 0;
  uint32_t rgb_without_alpha_samples = 0;
  uint64_t luminance_sum = 0;
  uint64_t luminance_squared_sum = 0;
  const UINT sample_step = 1u;
  for (UINT y = 0; y < height; y += sample_step) {
    for (UINT x = 0; x < width; x += sample_step) {
      uint8_t pixel[4] = {};
      rgba_at(x, y, pixel);
      const uint8_t coverage_alpha = effect_coverage(pixel);
      const bool covered = use_depth_coverage
                               ? has_written_depth(x, y)
                               : coverage_alpha > 4;
      if (!covered) {
        ++background_samples;
        continue;
      }
      ++content_samples;
      if (!use_depth_coverage) {
        minimum_alpha = std::min(minimum_alpha, pixel[3]);
        maximum_alpha = std::max(maximum_alpha, pixel[3]);
        const unsigned alpha_difference = static_cast<unsigned>(std::abs(
            static_cast<int>(pixel[3]) -
            static_cast<int>(background_rgba[3])));
        if (alpha_difference > 4) ++alpha_samples;
        if (alpha_difference <= 4 && coverage_alpha > 4)
          ++rgb_without_alpha_samples;
      }
      minimum_rgb = std::min({minimum_rgb, pixel[0], pixel[1], pixel[2]});
      maximum_rgb = std::max({maximum_rgb, pixel[0], pixel[1], pixel[2]});
      const uint32_t luminance =
          (54u * pixel[0] + 183u * pixel[1] + 19u * pixel[2]) >> 8;
      luminance_sum += luminance;
      luminance_squared_sum +=
          static_cast<uint64_t>(luminance) * luminance;
      min_x = std::min(min_x, x);
      min_y = std::min(min_y, y);
      max_x = std::max(max_x, x);
      max_y = std::max(max_y, y);
      found = true;
    }
  }
  g_native_capture.background_samples = background_samples;
  g_native_capture.content_samples = content_samples;
  g_native_capture.alpha_samples = alpha_samples;
  g_native_capture.rgb_without_alpha_samples = rgb_without_alpha_samples;
  g_native_capture.minimum_alpha = minimum_alpha;
  g_native_capture.maximum_alpha = maximum_alpha;
  g_native_capture.minimum_rgb = minimum_rgb;
  g_native_capture.maximum_rgb = maximum_rgb;
  double variance = 0.0;
  if (content_samples) {
    const double mean = static_cast<double>(luminance_sum) / content_samples;
    variance = static_cast<double>(luminance_squared_sum) / content_samples -
               mean * mean;
  }
  g_native_capture.luminance_stddev =
      static_cast<float>(std::sqrt(std::max(0.0, variance)));
  // A failed transparent-effect pass can carry no useful model image, so retain
  // the variation check there. Depth coverage itself is proof that Skyrim drew
  // real geometry and permits legitimately dark or nearly uniform materials.
  if (!found || content_samples < 64 || max_x <= min_x || max_y <= min_y ||
      max_x - min_x < 4 || max_y - min_y < 4 ||
      (!use_depth_coverage &&
       (maximum_rgb - minimum_rgb < 6 || variance < 4.0))) {
    // A fully off-screen model has no silhouette from which to calculate a
    // centroid. Every partially observed 1.5.97 inventory model in the live
    // trace entered from the right/bottom because the menu transform is
    // applied after the scene bounds used for framing. Search that proven
    // native offset in bounded camera-space steps. The frame is still accepted
    // only after Skyrim writes a complete authoritative depth silhouette.
    const float half_span_x = g_native_capture.camera_last_distance *
                              g_native_capture.camera_tangent_horizontal;
    const float half_span_z = g_native_capture.camera_last_distance *
                              g_native_capture.camera_tangent_vertical;
    if (g_native_capture.capture_attempts <= 4 &&
        std::isfinite(half_span_x) && std::isfinite(half_span_z) &&
        half_span_x > 0.0f && half_span_z > 0.0f) {
      const float old_x = g_native_capture.camera_pan_x;
      const float old_z = g_native_capture.camera_pan_z;
      g_native_capture.camera_pan_x = std::clamp(
          old_x - half_span_x * 0.55f, -half_span_x * 4.0f,
          half_span_x * 4.0f);
      g_native_capture.camera_pan_z = std::clamp(
          old_z - half_span_z * 0.30f, -half_span_z * 4.0f,
          half_span_z * 4.0f);
      ++g_native_capture.camera_pan_corrections;
      PluginLog(false,
                "[CAMERA_SEARCH] owner=ITEM_RENDER form=%08X attempt=%u "
                "pan=(%.2f,%.2f)->(%.2f,%.2f) span=(%.2f,%.2f)",
                g_native_capture.request.form_id,
                g_native_capture.capture_attempts, old_x, old_z,
                g_native_capture.camera_pan_x,
                g_native_capture.camera_pan_z, half_span_x, half_span_z);
    }
    g_native_capture.camera_fit_margin = std::min(
        6.0f, std::max(g_native_capture.camera_fit_margin + 0.25f,
                       g_native_capture.camera_fit_margin * 1.12f));
    return false;
  }
  g_native_capture.content_min_x = min_x;
  g_native_capture.content_min_y = min_y;
  g_native_capture.content_max_x = max_x;
  g_native_capture.content_max_y = max_y;
  // A non-empty frame is not necessarily a complete model.  The previous
  // path accepted geometry that crossed the render-target edge, cropped that
  // incomplete result, and enlarged it in the inventory panel.  That is what
  // made a sword, armor, or ring look like a raw texture swatch.  Reject the
  // frame and let the next native camera attempt back away instead.
  constexpr UINT kEdgeGuard = 4;
  const bool touches_left = min_x <= kEdgeGuard;
  const bool touches_top = min_y <= kEdgeGuard;
  const bool touches_right = max_x + kEdgeGuard >= width - 1;
  const bool touches_bottom = max_y + kEdgeGuard >= height - 1;
  const bool touches_edge = touches_left || touches_top || touches_right ||
                            touches_bottom;
  const bool spans_width = touches_left && touches_right;
  const bool spans_height = touches_top && touches_bottom;
  // A full-target depth write is not a fitted inventory object. It indicates
  // a renderer pass/state error, and publishing it recreates the opaque
  // texture-card failure this mask is designed to prevent. Camera retries can
  // recover ordinary oversized objects; a terminal full frame must fail.
  const uint64_t target_pixels =
      static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
  const bool implausibly_full_depth = use_depth_coverage &&
      static_cast<uint64_t>(depth_samples) * 100u >= target_pixels * 92u;
  if (implausibly_full_depth) {
    PluginLog(false,
              "[ITEM_FRAME] rejecting full-target depth form=%08X "
              "attempt=%u depth=%u/%llu",
              g_native_capture.request.form_id,
              g_native_capture.capture_attempts, depth_samples,
              static_cast<unsigned long long>(target_pixels));
    g_native_capture.camera_fit_margin = std::min(
        6.0f, std::max(g_native_capture.camera_fit_margin + 0.50f,
                       g_native_capture.camera_fit_margin * 1.70f));
    return false;
  }
  // A complete silhouette does not need a second GPU pass merely because it
  // is off-center: the authoritative depth crop below centers and aspect-fits
  // it into the fixed preview canvas. Feedback is only useful while an edge
  // proves some of the real mesh is still outside the capture target.
  const bool camera_corrected = touches_edge &&
      ApplySilhouetteCameraCorrection(
          "ITEM_RENDER", width, height, min_x, min_y, max_x, max_y,
          true, g_native_capture.camera_last_distance,
          g_native_capture.camera_tangent_horizontal,
          g_native_capture.camera_tangent_vertical,
          &g_native_capture.camera_pan_x, &g_native_capture.camera_pan_z,
          &g_native_capture.camera_pan_corrections);
  if (touches_edge && use_depth_coverage) {
    // Depth is authoritative geometry coverage, not shader spill. If it reaches
    // an edge, the mesh is genuinely clipped; never publish and magnify that
    // fragment as though it were a complete inventory object.
    PluginLog(false,
              "[ITEM_FRAME] clipped depth silhouette form=%08X attempt=%u "
              "bounds=%u,%u..%u,%u depth=%u; rejecting",
              g_native_capture.request.form_id,
              g_native_capture.capture_attempts, min_x, min_y, max_x, max_y,
              depth_samples);
    // When both opposing edges are occupied the clipped silhouette's center
    // is unknowable; panning cannot recover it. Back away decisively. For a
    // one-sided clip, combine a damped pan with enough zoom-out to reveal the
    // previously hidden half on the next one or two native passes.
    const float widen = (spans_width || spans_height)
        ? 1.38f
        : (camera_corrected ? 1.18f : 1.25f);
    g_native_capture.camera_fit_margin = std::min(
        6.0f, std::max(g_native_capture.camera_fit_margin + 0.15f,
                       g_native_capture.camera_fit_margin * widen));
    return false;
  }
  if (touches_edge &&
      g_native_capture.capture_attempts < kNativeCaptureMaxAttempts) {
    if (g_native_capture.capture_attempts <= 3) {
      PluginLog(false,
                "[ITEM_FRAME] clipped native model form=%08X "
                "attempt=%u bounds=%u,%u..%u,%u; retrying wider",
                g_native_capture.request.form_id,
                g_native_capture.capture_attempts, min_x, min_y, max_x,
                max_y);
    }
    const float widen = (spans_width || spans_height)
        ? 1.38f
        : (camera_corrected ? 1.18f : 1.25f);
    g_native_capture.camera_fit_margin = std::min(
        6.0f, std::max(g_native_capture.camera_fit_margin + 0.15f,
                       g_native_capture.camera_fit_margin * widen));
    return false;
  }
  if (touches_edge) {
    // With no native depth silhouette, edge-to-edge effect coverage is the
    // exact ambiguous full-frame case. Never publish it as an item image.
    PluginLog(false,
              "[ITEM_FRAME] rejecting terminal effect frame form=%08X "
              "bounds=%u,%u..%u,%u",
              g_native_capture.request.form_id, min_x, min_y, max_x, max_y);
    return false;
  }
  (void)camera_corrected;
  const UINT pad = 8;
  min_x = min_x > pad ? min_x - pad : 0;
  min_y = min_y > pad ? min_y - pad : 0;
  max_x = std::min(width - 1, max_x + pad);
  max_y = std::min(height - 1, max_y + pad);
  const float source_w = static_cast<float>(max_x - min_x + 1);
  const float source_h = static_cast<float>(max_y - min_y + 1);
  constexpr int output = static_cast<int>(dis::kNativeItemPreviewSize);
  // Preserve breathing room after the native camera fit. A six-pixel border
  // made long weapons and armour read as cropped texture swatches once the
  // private render was reduced into the bottom-screen detail card.
  constexpr int margin = 8;
  const float scale = std::min((output - margin * 2) / source_w,
                               (output - margin * 2) / source_h);
  const int drawn_w = std::max(1, static_cast<int>(source_w * scale));
  const int drawn_h = std::max(1, static_cast<int>(source_h * scale));
  const int start_x = (output - drawn_w) / 2;
  const int start_y = (output - drawn_h) / 2;
  std::memset(g_native_preview_rgba, 0, sizeof(g_native_preview_rgba));
  for (int y = 0; y < drawn_h; ++y) {
    const UINT sy = std::min(
        max_y, min_y + static_cast<UINT>(y / std::max(scale, 0.0001f)));
    for (int x = 0; x < drawn_w; ++x) {
      const UINT sx = std::min(
          max_x, min_x + static_cast<UINT>(x / std::max(scale, 0.0001f)));
      uint8_t pixel[4] = {};
      rgba_at(sx, sy, pixel);
      uint8_t output_alpha = 255;
      if (use_depth_coverage) {
        if (!has_written_depth(sx, sy)) continue;
      } else {
        output_alpha = effect_coverage(pixel);
        if (output_alpha <= 4) continue;
      }
      uint8_t* out = g_native_preview_rgba +
          (static_cast<size_t>(start_y + y) * output + start_x + x) * 4;
      if (use_depth_coverage) {
        out[0] = pixel[0];
        out[1] = pixel[1];
        out[2] = pixel[2];
      } else {
        // The private target contains premultiplied colour because Skyrim has
        // already blended the particles over transparent black. Convert it to
        // the straight RGBA expected by Canvas::BlitRgba; otherwise the UI
        // applies alpha a second time and makes the real effect nearly vanish.
        for (unsigned channel = 0; channel < 3; ++channel) {
          out[channel] = static_cast<uint8_t>(std::min<unsigned>(
              255u, (static_cast<unsigned>(pixel[channel]) * 255u +
                     output_alpha / 2u) /
                        output_alpha));
        }
      }
      out[3] = output_alpha;
    }
  }
  return true;
}

// InventoryMenu normally supplies UI3DSceneManager's camera through its
// Scaleform callbacks.  A preview requested while that menu is closed still
// loads and attaches a valid model, but Inventory3DManager::Render does not
// initialize that camera itself.  Frame the exact loaded model here and
// restore Skyrim's camera immediately after the private render.
bool ConfigureInventoryPreviewCamera(void* scene_manager, void* scene_model,
                                     float fit_margin,
                                     float* framed_distance = nullptr) {
  if (!scene_manager || !scene_model ||
      !RegionAllows(scene_manager, 0x118) ||
      !RegionAllows(scene_model, 0xF4))
    return false;
  void* camera = ReadOr<void*>(
      reinterpret_cast<uint8_t*>(scene_manager) + 0x20, nullptr);
  if (!camera || !RegionAllows(camera, sizeof(void*))) return false;

  VisibleGeometryBounds bounds = {};
  const bool geometry_measured =
      MeasureVisibleGeometry(scene_model, &bounds, 10000.0f, 100000.0f,
                             true);
  if (!geometry_measured) {
    float root_bound[4] = {};
    if (!Read(reinterpret_cast<uint8_t*>(scene_model) + 0xE4,
              &root_bound) ||
        !std::isfinite(root_bound[0]) ||
        !std::isfinite(root_bound[1]) ||
        !std::isfinite(root_bound[2]) ||
        !std::isfinite(root_bound[3]) || root_bound[3] <= 0.01f ||
        root_bound[3] > 100000.0f)
      return false;
    for (unsigned axis = 0; axis < 3; ++axis) {
      bounds.minimum[axis] = root_bound[axis] - root_bound[3];
      bounds.maximum[axis] = root_bound[axis] + root_bound[3];
    }
    bounds.valid = true;
    bounds.visited = 1;
    bounds.bounded = 1;
  }

  using SetPositionFn = void(*)(void*, const float*);
  using LookAtFn = void(*)(void*, const float*);
  auto set_position = reinterpret_cast<SetPositionFn>(
      g_base + kUI3DSetCameraPosition);
  auto look_at = reinterpret_cast<LookAtFn>(g_base + kUI3DLookAt);
  if (!RegionAllows(reinterpret_cast<void*>(set_position), 1, true) ||
      !RegionAllows(reinterpret_cast<void*>(look_at), 1, true))
    return false;

  constexpr float kFovParameter = 42.0f;
  float position[3] = {};
  float target[3] = {};
  float distance = 0.0f;
  float tangent_horizontal = 0.0f;
  float tangent_vertical = 0.0f;
  // Oversampling changes only the number of target pixels. Multiplying the
  // frustum slopes as well zoomed the actor out by kCharacterCaptureScale; the
  // live capture fell from the working ~340-pixel silhouette to ~116 pixels.
  // Keep Skyrim's native 45-degree projection while rendering it at 3x detail.
  if (!SetPrivateUi3DCameraFov(
          scene_manager, camera, kFovParameter, g_native_capture.width,
          g_native_capture.height, 1.0f,
          // The capture target is square and the live depth silhouettes prove
          // there is no later 16:9 expansion to cancel. Feeding the primary
          // swap-chain aspect here made every opaque item exactly 1.78x too
          // narrow (for example a 21.6x27.7 armor bound became 156x356).
          // Spells and ordinary inventory models must share the same square
          // camera projection.
          1.0f,
          &tangent_horizontal, &tangent_vertical) ||
      !ComputePerspectiveFit(bounds, g_native_capture.width,
                             g_native_capture.height, tangent_horizontal,
                             tangent_vertical, fit_margin, 1.0f, 40.0f,
                             target, position, &distance)) {
    return false;
  }
  target[0] += g_native_capture.camera_pan_x;
  target[2] += g_native_capture.camera_pan_z;
  position[0] += g_native_capture.camera_pan_x;
  position[2] += g_native_capture.camera_pan_z;
  set_position(scene_manager, position);
  // Skyrim's UI3D wrapper calls NiCamera::LookAt with its native +Z up vector
  // and refreshes the camera immediately. This preserves the engine's +X
  // forward convention without duplicating its matrix construction.
  look_at(scene_manager, target);
  g_native_capture.camera_last_distance = distance;
  g_native_capture.camera_tangent_horizontal = tangent_horizontal;
  g_native_capture.camera_tangent_vertical = tangent_vertical;
  g_native_capture.camera_expected_width =
      g_native_capture.width *
      (bounds.maximum[0] - bounds.minimum[0]) /
      std::max(0.001f, 2.0f * distance * tangent_horizontal);
  g_native_capture.camera_expected_height =
      g_native_capture.height *
      (bounds.maximum[2] - bounds.minimum[2]) /
      std::max(0.001f, 2.0f * distance * tangent_vertical);
  if (framed_distance) *framed_distance = distance;
  if (g_native_capture.capture_attempts <= 2) {
    PluginLog(false,
              "[ITEM_FRAME] native geometry form=%08X attempt=%u "
              "scene=%u/%u vertices=%u/%u bounds=(%.1f,%.1f,%.1f).."
              "(%.1f,%.1f,%.1f) target=(%.1f,%.1f,%.1f) "
              "distance=%.1f margin=%.2f slopes=(%.6f,%.6f) "
              "pan=(%.2f,%.2f)",
              g_native_capture.request.form_id,
              g_native_capture.capture_attempts, bounds.visited,
              bounds.geometry, bounds.vertex_geometries,
              bounds.sampled_vertices, bounds.minimum[0], bounds.minimum[1],
              bounds.minimum[2], bounds.maximum[0], bounds.maximum[1],
              bounds.maximum[2], target[0], target[1], target[2], distance,
              fit_margin, tangent_horizontal, tangent_vertical,
              g_native_capture.camera_pan_x,
              g_native_capture.camera_pan_z);
  }
  return true;
}

// UI3D replaces the complete fixed-function/input and VS/PS/GS binding set,
// not only the output merger and rasterizer. Keep the capture bounded and
// allocation-free; Skyrim's UI/material path uses no dynamic class instances,
// and its relevant SRV bindings are confined to the first 16 slots.
struct Ui3DPipelineState {
  ID3D11InputLayout* input_layout = nullptr;
  ID3D11Buffer* vertex_buffers[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
  UINT vertex_strides[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
  UINT vertex_offsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
  ID3D11Buffer* index_buffer = nullptr;
  DXGI_FORMAT index_format = DXGI_FORMAT_UNKNOWN;
  UINT index_offset = 0;
  D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;

  ID3D11VertexShader* vertex_shader = nullptr;
  ID3D11PixelShader* pixel_shader = nullptr;
  ID3D11GeometryShader* geometry_shader = nullptr;
  ID3D11Buffer* vertex_constants[
      D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
  ID3D11Buffer* pixel_constants[
      D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
  ID3D11Buffer* geometry_constants[
      D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
  static constexpr UINT kResourceSlots = 16;
  ID3D11ShaderResourceView* vertex_resources[kResourceSlots] = {};
  ID3D11ShaderResourceView* pixel_resources[kResourceSlots] = {};
  ID3D11ShaderResourceView* geometry_resources[kResourceSlots] = {};
  ID3D11SamplerState* vertex_samplers[
      D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
  ID3D11SamplerState* pixel_samplers[
      D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
  ID3D11SamplerState* geometry_samplers[
      D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};

  void Capture(ID3D11DeviceContext* context) {
    context->IAGetInputLayout(&input_layout);
    context->IAGetVertexBuffers(
        0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, vertex_buffers,
        vertex_strides, vertex_offsets);
    context->IAGetIndexBuffer(&index_buffer, &index_format, &index_offset);
    context->IAGetPrimitiveTopology(&topology);
    context->VSGetShader(&vertex_shader, nullptr, nullptr);
    context->PSGetShader(&pixel_shader, nullptr, nullptr);
    context->GSGetShader(&geometry_shader, nullptr, nullptr);
    context->VSGetConstantBuffers(
        0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,
        vertex_constants);
    context->PSGetConstantBuffers(
        0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,
        pixel_constants);
    context->GSGetConstantBuffers(
        0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,
        geometry_constants);
    context->VSGetShaderResources(0, kResourceSlots, vertex_resources);
    context->PSGetShaderResources(0, kResourceSlots, pixel_resources);
    context->GSGetShaderResources(0, kResourceSlots, geometry_resources);
    context->VSGetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT,
                           vertex_samplers);
    context->PSGetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT,
                           pixel_samplers);
    context->GSGetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT,
                           geometry_samplers);
  }

  void Restore(ID3D11DeviceContext* context) {
    context->IASetInputLayout(input_layout);
    context->IASetVertexBuffers(
        0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, vertex_buffers,
        vertex_strides, vertex_offsets);
    context->IASetIndexBuffer(index_buffer, index_format, index_offset);
    context->IASetPrimitiveTopology(topology);
    context->VSSetShader(vertex_shader, nullptr, 0);
    context->PSSetShader(pixel_shader, nullptr, 0);
    context->GSSetShader(geometry_shader, nullptr, 0);
    context->VSSetConstantBuffers(
        0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,
        vertex_constants);
    context->PSSetConstantBuffers(
        0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,
        pixel_constants);
    context->GSSetConstantBuffers(
        0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,
        geometry_constants);
    context->VSSetShaderResources(0, kResourceSlots, vertex_resources);
    context->PSSetShaderResources(0, kResourceSlots, pixel_resources);
    context->GSSetShaderResources(0, kResourceSlots, geometry_resources);
    context->VSSetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT,
                           vertex_samplers);
    context->PSSetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT,
                           pixel_samplers);
    context->GSSetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT,
                           geometry_samplers);
  }

  ~Ui3DPipelineState() {
    if (input_layout) input_layout->Release();
    for (auto* resource : vertex_buffers)
      if (resource) resource->Release();
    if (index_buffer) index_buffer->Release();
    if (vertex_shader) vertex_shader->Release();
    if (pixel_shader) pixel_shader->Release();
    if (geometry_shader) geometry_shader->Release();
    for (auto* resource : vertex_constants)
      if (resource) resource->Release();
    for (auto* resource : pixel_constants)
      if (resource) resource->Release();
    for (auto* resource : geometry_constants)
      if (resource) resource->Release();
    for (auto* resource : vertex_resources)
      if (resource) resource->Release();
    for (auto* resource : pixel_resources)
      if (resource) resource->Release();
    for (auto* resource : geometry_resources)
      if (resource) resource->Release();
    for (auto* resource : vertex_samplers)
      if (resource) resource->Release();
    for (auto* resource : pixel_samplers)
      if (resource) resource->Release();
    for (auto* resource : geometry_samplers)
      if (resource) resource->Release();
  }
};

bool CaptureInventoryModel(IDXGISwapChain* chain, void* manager) {
  if (!chain || !manager) return false;
  if (g_native_capture.readback_ready) {
    const bool converted = g_native_capture.readback_success;
    g_native_capture.readback_ready = false;
    g_native_capture.readback_success = false;
    g_native_capture.engine_render_result = 1;
    return converted;
  }
  if (g_native_capture.readback_pending) {
    g_native_capture.engine_render_result = 2;
    return false;
  }
  // A character lease normally lasts only from HUD PostDisplay to the same
  // frame's Present. Never let a second producer replace its process-global
  // framebuffer slot if platform scheduling ever crosses those callbacks.
  if (g_deferred_ui3d_target.active) {
    g_native_capture.engine_render_result = 2;
    return false;
  }
  ID3D11Device* device = nullptr;
  if (FAILED(chain->GetDevice(__uuidof(ID3D11Device),
                              reinterpret_cast<void**>(&device))) || !device)
    return false;
  ID3D11DeviceContext* context = nullptr;
  ID3D11Texture2D* back_buffer = nullptr;
  device->GetImmediateContext(&context);
  const HRESULT buffer_result = chain->GetBuffer(
      0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back_buffer));
  if (FAILED(buffer_result) || !context || !back_buffer) {
    if (back_buffer) back_buffer->Release();
    if (context) context->Release();
    device->Release();
    return false;
  }
  D3D11_TEXTURE2D_DESC desc = {};
  back_buffer->GetDesc(&desc);
  if (!EnsureNativeCaptureResources(chain, device, back_buffer, desc)) {
    back_buffer->Release();
    context->Release();
    device->Release();
    return false;
  }

  ID3D11RenderTargetView* old_views[
      D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
  ID3D11DepthStencilView* old_depth = nullptr;
  context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
                              old_views, &old_depth);
  ID3D11BlendState* old_blend = nullptr;
  FLOAT old_blend_factor[4] = {};
  UINT old_sample_mask = 0xFFFFFFFFu;
  context->OMGetBlendState(&old_blend, old_blend_factor, &old_sample_mask);
  ID3D11DepthStencilState* old_depth_state = nullptr;
  UINT old_stencil_reference = 0;
  context->OMGetDepthStencilState(&old_depth_state, &old_stencil_reference);
  ID3D11RasterizerState* old_rasterizer = nullptr;
  context->RSGetState(&old_rasterizer);
  UINT viewport_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
  D3D11_VIEWPORT old_viewports[
      D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
  context->RSGetViewports(&viewport_count, old_viewports);
  UINT scissor_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
  D3D11_RECT old_scissors[
      D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
  context->RSGetScissorRects(&scissor_count, old_scissors);
  Ui3DPipelineState pipeline_state;
  pipeline_state.Capture(context);

  auto release_saved_state = [&]() {
    if (old_rasterizer) old_rasterizer->Release();
    if (old_depth_state) old_depth_state->Release();
    if (old_blend) old_blend->Release();
    if (old_depth) old_depth->Release();
    for (auto* view : old_views)
      if (view) view->Release();
  };

  auto* renderer = reinterpret_cast<uint8_t*>(g_base + kRendererSingleton);
  auto* render_target_manager =
      reinterpret_cast<uint8_t*>(g_base + kRenderTargetManager);
  auto* framebuffer = reinterpret_cast<RenderTargetDataRaw*>(
      renderer + kRendererFramebufferOffset);
  auto* framebuffer_width = reinterpret_cast<uint32_t*>(
      render_target_manager + kRendererFramebufferWidthOffset);
  auto* framebuffer_height = reinterpret_cast<uint32_t*>(
      render_target_manager + kRendererFramebufferHeightOffset);
  auto* depth_stencil_width = reinterpret_cast<uint32_t*>(
      render_target_manager + kRendererDepthStencilWidthOffset);
  auto* depth_stencil_height = reinterpret_cast<uint32_t*>(
      render_target_manager + kRendererDepthStencilHeightOffset);
  if (!RegionWritable(framebuffer, sizeof(*framebuffer)) ||
      !RegionWritable(framebuffer_width, sizeof(*framebuffer_width)) ||
      !RegionWritable(framebuffer_height, sizeof(*framebuffer_height)) ||
      !RegionWritable(depth_stencil_width, sizeof(*depth_stencil_width)) ||
      !RegionWritable(depth_stencil_height, sizeof(*depth_stencil_height))) {
    release_saved_state();
    back_buffer->Release();
    context->Release();
    device->Release();
    return false;
  }
  const RenderTargetDataRaw saved_framebuffer = *framebuffer;
  const uint32_t saved_framebuffer_width = *framebuffer_width;
  const uint32_t saved_framebuffer_height = *framebuffer_height;
  const uint32_t saved_depth_stencil_width = *depth_stencil_width;
  const uint32_t saved_depth_stencil_height = *depth_stencil_height;
  float camera_distance = 0.0f;
  void* scene_manager = nullptr;
  int32_t* current_scene_scheme = nullptr;
  ScopedPrivateUi3DCamera private_camera;

  auto restore = [&]() {
    *framebuffer = saved_framebuffer;
    *framebuffer_width = saved_framebuffer_width;
    *framebuffer_height = saved_framebuffer_height;
    *depth_stencil_width = saved_depth_stencil_width;
    *depth_stencil_height = saved_depth_stencil_height;
    private_camera.Restore();
    context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
                                old_views, old_depth);
    context->OMSetBlendState(old_blend, old_blend_factor, old_sample_mask);
    context->OMSetDepthStencilState(old_depth_state, old_stencil_reference);
    context->RSSetState(old_rasterizer);
    context->RSSetViewports(viewport_count,
                            viewport_count ? old_viewports : nullptr);
    context->RSSetScissorRects(scissor_count,
                               scissor_count ? old_scissors : nullptr);
    pipeline_state.Restore(context);
  };
  RenderTargetDataRaw preview_target = {};
  preview_target.texture = g_native_capture.render_texture;
  preview_target.texture_copy = g_native_capture.render_texture;
  preview_target.render_view = g_native_capture.render_view;
  preview_target.shader_view = g_native_capture.shader_view;
  preview_target.shader_copy = g_native_capture.shader_view;
  *framebuffer = preview_target;
  *framebuffer_width = g_native_capture.width;
  *framebuffer_height = g_native_capture.height;
  *depth_stencil_width = g_native_capture.width;
  *depth_stencil_height = g_native_capture.height;

  context->OMSetRenderTargets(1, &g_native_capture.render_view,
                              g_native_capture.depth_view);
  const D3D11_VIEWPORT full = {
      0.0f, 0.0f, static_cast<float>(g_native_capture.width),
      static_cast<float>(g_native_capture.height), 0.0f, 1.0f};
  context->RSSetViewports(1, &full);
  const D3D11_RECT full_scissor = {
      0, 0, static_cast<LONG>(g_native_capture.width),
      static_cast<LONG>(g_native_capture.height)};
  context->RSSetScissorRects(1, &full_scissor);
  // Transparent Skyrim effects must be composed over transparent black. An
  // opaque magenta key permanently contaminates every partially transparent
  // particle before readback and cannot be removed by testing only exact clear
  // pixels. Geometry transparency continues to come from authoritative depth.
  const float transparent_black[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  context->ClearRenderTargetView(g_native_capture.render_view,
                                 transparent_black);
  context->ClearDepthStencilView(g_native_capture.depth_view,
                                 D3D11_CLEAR_DEPTH, 1.0f, 0);
  using InventoryUpdateTransformFn = void(*)(void*);
  auto inventory_update_transform =
      reinterpret_cast<InventoryUpdateTransformFn>(
          g_base + kInventory3DUpdateTransform);
  if (!RegionAllows(reinterpret_cast<void*>(inventory_update_transform), 1,
                    true)) {
    restore();
    release_saved_state();
    back_buffer->Release();
    context->Release();
    device->Release();
    return false;
  }
  // Match Inventory3DManager::Render's ordering without advancing its native
  // transition twice in one HUD frame. RVA 0x888A80 is not idempotent while
  // manager+0x148 is non-zero: it advances manager+0x14C by frame delta before
  // applying the model transform. Settle one native step, and defer the draw
  // until the transition reaches zero. The Render call below then invokes the
  // helper as a proven no-op, so the bounds used for camera fitting are the
  // same bounds that Skyrim submits.
  inventory_update_transform(manager);
  const float transform_rate = ReadOr<float>(
      reinterpret_cast<uint8_t*>(manager) + 0x148, 0.0f);
  if (!std::isfinite(transform_rate) || std::abs(transform_rate) > 0.00001f) {
    const uint64_t settle_elapsed =
        g_native_capture.capture_started_ms
            ? GetTickCount64() - g_native_capture.capture_started_ms
            : 0;
    // Transition settling is not a failed camera/draw attempt. Preserve the
    // bounded capture-attempt budget while the native animation is making forward
    // progress; after the retry window, let attempts count out and fail safely
    // rather than double-applying a stuck transform.
    if (settle_elapsed < kNativeCaptureRetryWindowMs &&
        g_native_capture.capture_attempts)
      --g_native_capture.capture_attempts;
    if (g_native_capture.capture_attempts <= 1) {
      PluginLog(false,
                "[ITEM_FRAME] waiting for native transform form=%08X "
                "rate=%.6f elapsed=%llums",
                g_native_capture.request.form_id, transform_rate,
                static_cast<unsigned long long>(settle_elapsed));
    }
    restore();
    release_saved_state();
    back_buffer->Release();
    context->Release();
    device->Release();
    return false;
  }
  void* scene_model = FindInventorySceneModel(
      manager, g_native_capture.entry.object);
  const int32_t scheme = InventorySceneScheme(scene_model);
  g_native_capture.rendered_scheme = scheme;
  g_native_capture.engine_render_result = 0;
  scene_manager = ReadOr<void*>(
      reinterpret_cast<void*>(g_base + kUI3DSceneManagerPtr), nullptr);
  current_scene_scheme = scene_manager
      ? reinterpret_cast<int32_t*>(
            reinterpret_cast<uint8_t*>(scene_manager) + 0x90)
      : nullptr;
  using InventoryRenderFn = uint32_t(*)(void*);
  auto inventory_render = reinterpret_cast<InventoryRenderFn>(
      g_base + kInventory3DRender);
  const int32_t manager_scheme = ReadOr<int32_t>(
      reinterpret_cast<uint8_t*>(manager) + 0x34, -1);
  GpuPipelineProbe pipeline_probe;
  bool render_ready = scene_model && scheme == kItemPreviewScheme &&
      manager_scheme == kItemPreviewScheme && scene_manager &&
      RegionAllows(scene_manager, 0x118) &&
      RegionAllows(reinterpret_cast<void*>(inventory_render), 1, true);
  if (render_ready) {
    // Native Inventory3DManager initialization placed scheme 1 on the UI3D
    // stack and made it current. A mismatch now means another menu took the
    // process-global renderer; never repair its light state by raw writes.
    render_ready = ReadOr<int32_t>(current_scene_scheme, -1) ==
                   kItemPreviewScheme;
  }
  if (render_ready) {
    const float fit_margin = std::clamp(
        g_native_capture.camera_fit_margin, 1.15f, 6.0f);
    render_ready = private_camera.Acquire(scene_manager) &&
        ConfigureInventoryPreviewCamera(
            scene_manager, scene_model, fit_margin, &camera_distance);
    if (render_ready && g_native_capture.capture_attempts == 1)
      LogUi3DSceneState("[ITEM_RENDER]", scene_manager, scene_model);
    if (!render_ready) {
      PluginLog(false,
                "item-preview: camera framing unavailable form=%08X "
                "scene=%p attempt=%u",
                g_native_capture.request.form_id, scene_model,
                g_native_capture.capture_attempts);
    }
  }
  if (render_ready) {
    // Inventory3DManager::Render applies the manager's native model transform
    // and then dispatches UI3DSceneManager::Render using manager+0x34. It does
    // not initialize the UI3D camera; InventoryMenu normally does that through
    // its Scaleform callbacks. The native renderer also hard-gates drawing on
    // UI::numDisablePauseMenu, which InventoryMenu normally increments via its
    // kDisablePauseMenu flag. Reproduce that exact precondition only for this
    // private render call; do not open, pause, or add a fake menu to the stack.
    const UiRendererCounters counters_before = ReadUiRendererCounters();
    LONG permission_value = 0;
    const bool diagnostic_render =
        g_native_item_render_diagnostic_frames.fetch_add(
            1, std::memory_order_acq_rel) < 2;
    const bool culling_hook_ready =
        diagnostic_render && InstallUi3DTraceHooks(scene_manager);
    const bool trace_render = diagnostic_render && culling_hook_ready;
    {
      ScopedUi3DRenderPermission ui3d_permission;
      render_ready = render_ready && static_cast<bool>(ui3d_permission);
      if (render_ready) {
        ScopedPrivateUi3DRender private_render(
            DeferredUi3DTargetOwner::kItem, g_native_capture.width,
            g_native_capture.height, g_native_capture.render_view,
            g_native_capture.depth_view);
        permission_value = ui3d_permission.observed_value();
        if (diagnostic_render)
          pipeline_probe.Start(device, context);
        if (trace_render) {
          BeginUi3DFrameTrace(DeferredUi3DTargetOwner::kItem,
                              scene_manager, scene_model,
                              g_native_capture.render_view);
          LogUi3DObjectState("[UI3D_SUBJECT_BEFORE_DISPATCH]", scene_model,
                             0);
          LogUi3DControllerChain(
              "[UI3D_SUBJECT_CONTROLLER_BEFORE_DISPATCH]", scene_model);
        }
        g_native_capture.engine_render_result = inventory_render(manager);
        if (trace_render) EndUi3DFrameTrace();
        pipeline_probe.Stop(context);
        if (diagnostic_render) {
          LogGpuOutputBinding("[ITEM_RENDER]", context,
                              g_native_capture.render_view,
                              g_native_capture.render_texture,
                              *framebuffer_width, *framebuffer_height);
        }
      } else {
        PluginLog(false,
                  "[ITEM_RENDER] UI3D permission unavailable form=%08X "
                  "ui_valid=%u disable_pause=%u custom=%u gate_failures=%llu",
                  g_native_capture.request.form_id,
                  counters_before.valid ? 1u : 0u,
                  counters_before.disable_pause,
                  counters_before.custom_rendering,
                  static_cast<unsigned long long>(
                      g_ui3d_gate_failures.load(std::memory_order_relaxed)));
      }
    }
    if (diagnostic_render) {
      PluginLog(false,
                "[ITEM_RENDER] render form=%08X scheme=%d "
                "camera_distance=%.1f attempt=%u result=%u ui_valid=%u "
                "disable_pause_before=%u custom=%u permission_value=%ld",
                g_native_capture.request.form_id, scheme, camera_distance,
                g_native_capture.capture_attempts,
                g_native_capture.engine_render_result,
                counters_before.valid ? 1u : 0u,
                counters_before.disable_pause,
                counters_before.custom_rendering,
                static_cast<long>(permission_value));
    }
  }
  // The accumulator hook proves that culling, geometry append, and draw
  // submission all finish synchronously inside Inventory3DManager::Render.
  // Restore the private camera pointer, renderer metadata, output target,
  // viewport, and pipeline now; Present only performs the ordered texture
  // readback.
  restore();
  if (render_ready && g_native_capture.engine_render_result != 0) {
    g_native_capture.readback_pending = true;
    g_native_capture.readback_copy_issued = false;
    g_native_capture.readback_ready = false;
    g_native_capture.readback_success = false;
    g_native_capture.readback_dispatched_ms = GetTickCount64();
    g_native_capture.engine_render_result = 2;
    if (g_native_capture.capture_attempts <= 2) {
      PluginLog(false,
                "[GPU] [ITEM_RENDER] draw dispatched for Present readback "
                "form=%08X attempt=%u",
                g_native_capture.request.form_id,
                g_native_capture.capture_attempts);
    }
  }
  if (g_native_capture.capture_attempts <= 2)
    pipeline_probe.Log(context, "[ITEM_RENDER]");

  release_saved_state();
  back_buffer->Release();
  context->Release();
  device->Release();
  return false;
}

void CompleteActiveNativePreviewFailure(const char* reason) {
  if (!g_native_capture.request.form_id || g_native_capture.completion_sent)
    return;
  g_native_capture.completion_sent = true;
  PurgeQueuedNativePreview(g_native_capture.request.form_id,
                           g_native_capture.request.world_epoch);
  CompleteNativePreviewFailure(g_native_capture.request.form_id,
                               g_native_capture.request.world_epoch, reason);
}

void ClearNativeInventoryLifecycleToken() {
  g_native_capture.manager_lifecycle_owned = false;
  g_native_capture.owned_manager = nullptr;
  g_native_capture.owned_temp_reference = nullptr;
}

bool NativeInventoryLifecycleMatches(void* manager) {
  if (!g_native_capture.manager_lifecycle_owned || !manager ||
      manager != g_native_capture.owned_manager ||
      !RegionAllows(manager, 0x160))
    return false;
  return ReadOr<int32_t>(reinterpret_cast<uint8_t*>(manager) + 0x34, -1) ==
             kItemPreviewScheme &&
         ReadOr<void*>(reinterpret_cast<uint8_t*>(manager) + 0x38, nullptr) ==
             g_native_capture.owned_temp_reference;
}

enum class NativeLoadServiceResult : uint8_t {
  kReady,
  kPending,
  kCanceled,
  kInvalid,
  kOwnershipLost,
};

NativeLoadServiceResult ServiceCompletedNativeInventoryLoad(void* manager) {
  const InventoryLoadTaskSnapshot before = ReadInventoryLoadTask(manager);
  LogInventoryLoadTaskTransition(manager, "poll");
  if (!before.valid) return NativeLoadServiceResult::kInvalid;
  if (!before.task) return NativeLoadServiceResult::kReady;
  // UpdateMagic3D deliberately omits Inventory3DManager's temporary reference
  // for SpellItem/Shout loads (the 1.5.97 implementation passes a null fourth
  // constructor argument). Item loads retain the lifecycle-owned reference.
  void* expected_extra_context = g_native_capture.magic_preview
                                     ? nullptr
                                     : g_native_capture.owned_temp_reference;
  const bool exact_task_owner =
      g_native_capture.entry.object &&
      before.bound_object == g_native_capture.entry.object &&
      before.model_object &&
      before.extra_context == expected_extra_context;
  if (!exact_task_owner) {
    PluginLog(false,
              "[ITEM_LIFECYCLE] native load task ownership changed "
              "form=%08X task=%p bound=%p/%p model=%p context=%p/%p",
              g_native_capture.request.form_id, before.task,
              before.bound_object, g_native_capture.entry.object,
              before.model_object, before.extra_context,
              expected_extra_context);
    return NativeLoadServiceResult::kOwnershipLost;
  }
  if (before.state == 6) return NativeLoadServiceResult::kCanceled;
  if (before.state > 6) return NativeLoadServiceResult::kInvalid;
  if (before.state != 5) return NativeLoadServiceResult::kPending;

  // Inventory3DManager::Render consumes completed load state before it reaches
  // UI3DSceneManager::Render. Keep Skyrim's normal HUD gate closed here: the
  // adoption handler attaches the native model and releases manager+0x150,
  // while UI3D exits before drawing. The next HUD frame can then configure the
  // private camera and target around a stable scene model.
  if (g_native_capture.readback_pending)
    return NativeLoadServiceResult::kPending;
  if (!NativeInventoryLifecycleMatches(manager))
    return NativeLoadServiceResult::kInvalid;
  const UiRendererCounters counters = ReadUiRendererCounters();
  if (!counters.valid || counters.disable_pause != 0)
    return NativeLoadServiceResult::kPending;

  using InventoryRenderFn = uint32_t(*)(void*);
  auto render = reinterpret_cast<InventoryRenderFn>(
      g_base + kInventory3DRender);
  if (!RegionAllows(reinterpret_cast<void*>(render), 1, true))
    return NativeLoadServiceResult::kInvalid;

  // The call can release the completed task. Never dereference `before.task`
  // after this point; reacquire all state through the manager.
  const uint32_t render_result = render(manager);
  const InventoryLoadTaskSnapshot after = ReadInventoryLoadTask(manager);
  LoadedInventoryModelRaw* models = nullptr;
  uint32_t model_count = 0;
  const bool models_valid = InventoryModels(manager, &models, &model_count);
  PluginLog(false,
            "[ITEM_LOAD] adopted form=%08X before=%p state=%u after=%p "
            "models_valid=%u models=%u render_result=%u",
            g_native_capture.request.form_id, before.task, before.state,
            after.task, models_valid ? 1u : 0u, model_count, render_result);
  LogInventoryLoadTaskTransition(manager, "after-adoption");
  if (!after.valid || after.task) return NativeLoadServiceResult::kInvalid;
  return models_valid ? NativeLoadServiceResult::kReady
                      : NativeLoadServiceResult::kInvalid;
}

bool AcquireNativeInventoryLifecycle(void* manager) {
  if (!manager || !RegionAllows(manager, 0x160) ||
      g_native_capture.manager_lifecycle_owned)
    return false;
  LoadedInventoryModelRaw* models = nullptr;
  uint32_t model_count = 0;
  if (!InventoryModels(manager, &models, &model_count) || model_count != 0 ||
      InventoryLoadInFlight(manager) ||
      ReadOr<int32_t>(reinterpret_cast<uint8_t*>(manager) + 0x34, -1) != 0 ||
      ReadOr<void*>(reinterpret_cast<uint8_t*>(manager) + 0x38, nullptr))
    return false;

  using InitializeFn = void(*)(void*, int32_t);
  auto initialize = reinterpret_cast<InitializeFn>(
      g_base + kInventory3DInitialize);
  using ShutdownFn = void(*)(void*);
  auto shutdown = reinterpret_cast<ShutdownFn>(g_base + kInventory3DShutdown);
  auto update_item = reinterpret_cast<void*>(g_base + kInventory3DUpdateItem);
  auto update_magic = reinterpret_cast<void*>(g_base + kInventory3DUpdateMagic);
  if (!RegionAllows(reinterpret_cast<void*>(initialize), 1, true) ||
      !RegionAllows(reinterpret_cast<void*>(shutdown), 1, true) ||
      !RegionAllows(update_item, 1, true) ||
      !RegionAllows(update_magic, 1, true))
    return false;

  // InventoryMenu uses this complete constructor-like path.  Besides selecting
  // scheme 1 it initializes the scheme root, lights, canonical model transform,
  // temporary TESObjectREFR, event registration, and asynchronous task state.
  // Calling only UpdateItem3D after writing +0x34 omitted all of those required
  // preconditions and produced a valid model graph that never reached drawing.
  initialize(manager, kItemPreviewScheme);
  void* temp_reference = ReadOr<void*>(
      reinterpret_cast<uint8_t*>(manager) + 0x38, nullptr);
  const int32_t initialized_scheme = ReadOr<int32_t>(
      reinterpret_cast<uint8_t*>(manager) + 0x34, -1);
  if (initialized_scheme != kItemPreviewScheme || !temp_reference) {
    PluginLog(false,
              "[ITEM_LIFECYCLE] native init validation failed manager=%p "
              "scheme=%d temp=%p",
              manager, initialized_scheme, temp_reference);
    // The call began from a proven-neutral singleton on this UI thread.  If it
    // partially initialized, its paired native shutdown is the only safe
    // rollback; never repair individual fields by hand.
    if (initialized_scheme == kItemPreviewScheme) {
      shutdown(manager);
    }
    return false;
  }

  g_native_capture.manager_lifecycle_owned = true;
  g_native_capture.owned_manager = manager;
  g_native_capture.owned_temp_reference = temp_reference;
  void* scene_manager = ReadOr<void*>(
      reinterpret_cast<void*>(g_base + kUI3DSceneManagerPtr), nullptr);
  void* scheme_root = scene_manager && RegionAllows(scene_manager, 0x118)
                          ? ReadOr<void*>(
                                reinterpret_cast<uint8_t*>(scene_manager) +
                                    0x38 + kItemPreviewScheme * sizeof(void*),
                                nullptr)
                          : nullptr;
  PluginLog(false,
            "[ITEM_LIFECYCLE] native init acquired manager=%p scheme=%d "
            "temp=%p pos=(%.3f,%.3f,%.3f) scale=%.3f task=%p "
            "state148=%p enabled158=%u scene=%p current=%d stack=%u "
            "root=%p root_local=(%.3f,%.3f,%.3f)",
            manager, initialized_scheme, temp_reference,
            ReadOr<float>(reinterpret_cast<uint8_t*>(manager) + 0x14, 0.0f),
            ReadOr<float>(reinterpret_cast<uint8_t*>(manager) + 0x18, 0.0f),
            ReadOr<float>(reinterpret_cast<uint8_t*>(manager) + 0x1C, 0.0f),
            ReadOr<float>(reinterpret_cast<uint8_t*>(manager) + 0x2C, 0.0f),
            ReadOr<void*>(reinterpret_cast<uint8_t*>(manager) + 0x150,
                          nullptr),
            ReadOr<void*>(reinterpret_cast<uint8_t*>(manager) + 0x148,
                          nullptr),
            static_cast<unsigned>(ReadOr<uint8_t>(
                reinterpret_cast<uint8_t*>(manager) + 0x158, 0)),
            scene_manager,
            scene_manager
                ? ReadOr<int32_t>(
                      reinterpret_cast<uint8_t*>(scene_manager) + 0x90, -1)
                : -1,
            scene_manager
                ? ReadOr<uint32_t>(
                      reinterpret_cast<uint8_t*>(scene_manager) + 0xA8, 0)
                : 0,
            scheme_root,
            scheme_root
                ? ReadOr<float>(
                      reinterpret_cast<uint8_t*>(scheme_root) + 0x6C, 0.0f)
                : 0.0f,
            scheme_root
                ? ReadOr<float>(
                      reinterpret_cast<uint8_t*>(scheme_root) + 0x70, 0.0f)
                : 0.0f,
            scheme_root
                ? ReadOr<float>(
                      reinterpret_cast<uint8_t*>(scheme_root) + 0x74, 0.0f)
                : 0.0f);
  return true;
}

bool ReleaseNativeInventoryLifecycle(void* manager = nullptr) {
  if (!g_native_capture.manager_lifecycle_owned) return true;
  if (!manager)
    manager = ReadOr<void*>(
        reinterpret_cast<void*>(g_base + kInventory3DManagerPtr), nullptr);
  if (!manager || !RegionAllows(manager, 0x160) ||
      manager != g_native_capture.owned_manager) {
    PluginLog(false,
              "[ITEM_LIFECYCLE] manager disappeared/replaced; native shutdown "
              "cannot be called old=%p current=%p",
              g_native_capture.owned_manager, manager);
    ClearNativeInventoryLifecycleToken();
    return false;
  }
  if (!NativeInventoryLifecycleMatches(manager)) {
    PluginLog(false,
              "[ITEM_LIFECYCLE] ownership changed; shutdown skipped manager=%p "
              "scheme=%d temp=%p expected_temp=%p",
              manager,
              ReadOr<int32_t>(reinterpret_cast<uint8_t*>(manager) + 0x34, -1),
              ReadOr<void*>(reinterpret_cast<uint8_t*>(manager) + 0x38,
                            nullptr),
              g_native_capture.owned_temp_reference);
    g_native_capture.manager_ownership_lost = true;
    ClearNativeInventoryLifecycleToken();
    return false;
  }

  using ShutdownFn = void(*)(void*);
  auto shutdown = reinterpret_cast<ShutdownFn>(g_base + kInventory3DShutdown);
  if (!RegionAllows(reinterpret_cast<void*>(shutdown), 1, true)) return false;
  // Clear the token before invoking engine callbacks so a reentrant reset can
  // never call the paired shutdown twice.
  void* released_temp = g_native_capture.owned_temp_reference;
  ClearNativeInventoryLifecycleToken();
  shutdown(manager);
  g_native_capture.unload_called = true;
  const int32_t final_scheme = ReadOr<int32_t>(
      reinterpret_cast<uint8_t*>(manager) + 0x34, -1);
  void* final_temp = ReadOr<void*>(
      reinterpret_cast<uint8_t*>(manager) + 0x38, nullptr);
  PluginLog(false,
            "[ITEM_LIFECYCLE] native shutdown complete manager=%p "
            "released_temp=%p scheme=%d temp=%p task=%p models=%u",
            manager, released_temp, final_scheme, final_temp,
            ReadOr<void*>(reinterpret_cast<uint8_t*>(manager) + 0x150,
                          nullptr),
            ReadOr<uint32_t>(reinterpret_cast<uint8_t*>(manager) + 0x140, 0));
  return final_scheme == 0 && final_temp == nullptr;
}

bool NativeItemReadbackQuiescent() {
  return !g_native_capture.readback_pending &&
         !g_native_capture.readback_copy_issued;
}

bool NativeInventoryManagerNeutral(void* manager) {
  if (!manager || !RegionAllows(manager, 0x160) ||
      InventoryLoadInFlight(manager) ||
      ReadOr<int32_t>(reinterpret_cast<uint8_t*>(manager) + 0x34, -1) != 0 ||
      ReadOr<void*>(reinterpret_cast<uint8_t*>(manager) + 0x38, nullptr))
    return false;
  LoadedInventoryModelRaw* models = nullptr;
  uint32_t model_count = 0;
  return InventoryModels(manager, &models, &model_count) && model_count == 0;
}

void NoteNativeItemUnloadBlocked(const char* reason, void* manager,
                                 uint64_t now) {
  if (!g_native_capture.unload_started_ms)
    g_native_capture.unload_started_ms = now;
  const uint64_t age = now >= g_native_capture.unload_started_ms
                           ? now - g_native_capture.unload_started_ms
                           : 0;
  if (age >= 3000 &&
      (!g_native_capture.last_unload_log_ms ||
       now - g_native_capture.last_unload_log_ms >= 5000)) {
    LoadedInventoryModelRaw* models = nullptr;
    uint32_t model_count = 0;
    const bool models_valid =
        manager && RegionAllows(manager, 0x160) &&
        InventoryModels(manager, &models, &model_count);
    PluginLog(false,
              "[ITEM_LIFECYCLE] unload quarantined reason=%s age=%llums "
              "form=%08X manager=%p task=%p scheme=%d temp=%p "
              "models_valid=%u models=%u readback=%u copy=%u submitted=%u",
              reason ? reason : "not neutral",
              static_cast<unsigned long long>(age),
              g_native_capture.request.form_id, manager,
              manager && RegionAllows(manager, 0x160)
                  ? ReadOr<void*>(reinterpret_cast<uint8_t*>(manager) + 0x150,
                                  nullptr)
                  : nullptr,
              manager && RegionAllows(manager, 0x160)
                  ? ReadOr<int32_t>(
                        reinterpret_cast<uint8_t*>(manager) + 0x34, -1)
                  : -1,
              manager && RegionAllows(manager, 0x160)
                  ? ReadOr<void*>(reinterpret_cast<uint8_t*>(manager) + 0x38,
                                  nullptr)
                  : nullptr,
              models_valid ? 1u : 0u, model_count,
              g_native_capture.readback_pending ? 1u : 0u,
              g_native_capture.readback_copy_issued ? 1u : 0u,
              g_native_capture.entry_submitted ? 1u : 0u);
    g_native_capture.last_unload_log_ms = now;
  }
  if (age >= 5000 && !g_native_capture.unload_reset_requested) {
    g_native_capture.unload_reset_requested = true;
    g_native_render_reset_revision.fetch_add(1, std::memory_order_acq_rel);
    PluginLog(false,
              "[ITEM_LIFECYCLE] unload requested renderer reset without "
              "releasing engine-owned state form=%08X age=%llums",
              g_native_capture.request.form_id,
              static_cast<unsigned long long>(age));
  }
}

bool ResetNativeCapture(bool keep_resources = true) {
  void* manager = ReadOr<void*>(
      reinterpret_cast<void*>(g_base + kInventory3DManagerPtr), nullptr);
  const uint64_t now = GetTickCount64();
  // The event query is the ownership fence for the staging textures and any
  // model resources submitted by Inventory3DManager. No caller may turn a
  // submitted copy into an apparently idle state before Present drains it.
  if (!NativeItemReadbackQuiescent()) {
    g_native_capture.stage = NativeCaptureStage::kUnloading;
    NoteNativeItemUnloadBlocked("GPU readback still in flight", manager, now);
    return false;
  }
  if (g_native_capture.manager_lifecycle_owned) {
    const bool shutdown_complete = ReleaseNativeInventoryLifecycle(manager);
    if (!shutdown_complete &&
        (g_native_capture.manager_lifecycle_owned ||
         g_native_capture.entry_submitted)) {
      g_native_capture.stage = NativeCaptureStage::kUnloading;
      NoteNativeItemUnloadBlocked("native shutdown incomplete", manager, now);
      return false;
    }
  }
  // UpdateItem3D retains &entry asynchronously. Keep both request and entry
  // byte-for-byte intact until Skyrim proves the task, model array, scheme,
  // and temporary reference are all neutral. A timeout requests quarantine;
  // it is never permission to zero live engine-owned storage.
  if (g_native_capture.entry_submitted &&
      !NativeInventoryManagerNeutral(manager)) {
    g_native_capture.stage = NativeCaptureStage::kUnloading;
    NoteNativeItemUnloadBlocked("Inventory3DManager not neutral", manager,
                                now);
    return false;
  }

  CompleteActiveNativePreviewFailure("capture state reset");
  g_native_capture.stage = NativeCaptureStage::kIdle;
  ClearNativeInventoryLifecycleToken();
  g_native_capture.attached_model = nullptr;
  g_native_capture.attached_scheme = -1;
  g_native_capture.request = {};
  g_native_active_preview_form.store(0, std::memory_order_release);
  g_native_active_preview_epoch.store(0, std::memory_order_release);
  g_native_capture.entry = {};
  g_native_capture.started_ms = 0;
  g_native_capture.capture_started_ms = 0;
  g_native_capture.next_capture_attempt_ms = 0;
  g_native_capture.unload_started_ms = 0;
  g_native_capture.last_unload_log_ms = 0;
  g_native_capture.settled_frames = 0;
  g_native_capture.capture_attempts = 0;
  g_native_capture.unload_called = false;
  g_native_capture.completion_sent = false;
  g_native_capture.magic_preview = false;
  g_native_capture.entry_submitted = false;
  g_native_capture.unload_reset_requested = false;
  g_native_capture.manager_ownership_lost = false;
  g_native_capture.content_samples = 0;
  g_native_capture.background_samples = 0;
  g_native_capture.depth_samples = 0;
  g_native_capture.minimum_rgb = 255;
  g_native_capture.maximum_rgb = 0;
  g_native_capture.luminance_stddev = 0.0f;
  g_native_capture.readback_pending = false;
  g_native_capture.readback_copy_issued = false;
  g_native_capture.readback_ready = false;
  g_native_capture.readback_success = false;
  g_native_capture.readback_dispatched_ms = 0;
  g_native_capture.camera_fit_margin = 1.35f;
  g_native_capture.camera_pan_x = 0.0f;
  g_native_capture.camera_pan_z = 0.0f;
  g_native_capture.camera_last_distance = 0.0f;
  g_native_capture.camera_tangent_horizontal = 0.0f;
  g_native_capture.camera_tangent_vertical = 0.0f;
  g_native_capture.camera_pan_corrections = 0;
  g_native_capture.logged_load_task = nullptr;
  g_native_capture.logged_load_state = 0xFFFFFFFFu;
  g_native_capture.logged_load_phase = 0xFFFFFFFFu;
  g_native_capture.logged_load_pending = 0xFFFFFFFFu;
  if (!keep_resources) ReleaseNativeCaptureResources();
  return true;
}

void RequestNativeInventoryMenuHandoff(const char* source) {
  if (g_native_capture.stage == NativeCaptureStage::kIdle ||
      g_native_capture.stage == NativeCaptureStage::kUnloading)
    return;
  bool requeued = false;
  const uint64_t current_epoch =
      g_world_epoch.load(std::memory_order_acquire);
  if (g_native_capture.request.form_id &&
      !g_native_capture.completion_sent &&
      g_native_capture.request.world_epoch == current_epoch &&
      LiveWorldReadyForNativeRendering()) {
    QueueNativePreview(g_native_capture.request.form_id,
                       g_native_capture.request.world_epoch,
                       0, 0, false, true, true);
    // The priority queue now owns eventual completion. Suppress only the
    // cleanup-time failure from this interrupted custom transaction.
    g_native_capture.completion_sent = true;
    requeued = true;
  }
  g_native_capture.stage = NativeCaptureStage::kUnloading;
  g_native_capture.unload_started_ms = 0;
  PluginLog(false,
            "[ITEM_LIFECYCLE] native InventoryMenu requested exact custom "
            "manager handoff source=%s form=%08X requeued=%u readback=%u "
            "copy=%u lease=%u",
            source ? source : "unknown", g_native_capture.request.form_id,
            requeued ? 1u : 0u,
            g_native_capture.readback_pending ? 1u : 0u,
            g_native_capture.readback_copy_issued ? 1u : 0u,
            g_deferred_ui3d_target.active ? 1u : 0u);
}

bool ServiceNativeInventoryMenuHandoff(void* manager, const char* source) {
  RequestNativeInventoryMenuHandoff(source);
  if (g_native_capture.stage == NativeCaptureStage::kIdle) return true;

  const uint64_t now = GetTickCount64();
  if (!manager || !RegionAllows(manager, 0x160)) {
    NoteNativeItemUnloadBlocked("menu handoff manager unavailable", manager,
                                now);
    return false;
  }
  if (g_deferred_ui3d_target.active || !NativeItemReadbackQuiescent()) {
    NoteNativeItemUnloadBlocked("menu handoff GPU work", manager, now);
    return false;
  }
  if (g_native_capture.manager_ownership_lost) {
    // The native menu has already replaced our exact scheme/temp token. It is
    // now the authoritative singleton owner; never shut down its state. Keep
    // only the local request quarantined until the menu later becomes neutral.
    return true;
  }
  if (g_native_capture.manager_lifecycle_owned) {
    if (!ReleaseNativeInventoryLifecycle(manager)) {
      if (g_native_capture.manager_ownership_lost) return true;
      NoteNativeItemUnloadBlocked("menu handoff native shutdown", manager,
                                  now);
      return false;
    }
    g_native_capture.attached_model = nullptr;
    g_native_capture.attached_scheme = -1;
  }
  if (g_native_capture.entry_submitted &&
      !NativeInventoryManagerNeutral(manager)) {
    NoteNativeItemUnloadBlocked("menu handoff manager not neutral", manager,
                                now);
    return false;
  }
  if (!ResetNativeCapture()) return false;
  PluginLog(false,
            "[ITEM_LIFECYCLE] custom manager handoff complete source=%s",
            source ? source : "unknown");
  return true;
}

void TickNativeCapture(IDXGISwapChain* chain) {
  const uint64_t now = GetTickCount64();
  void* manager = ReadOr<void*>(
      reinterpret_cast<void*>(g_base + kInventory3DManagerPtr), nullptr);
  if (!manager || !RegionAllows(manager, 0x160)) {
    if (g_native_capture.stage != NativeCaptureStage::kIdle) {
      g_native_capture.stage = NativeCaptureStage::kUnloading;
      if (!NativeItemReadbackQuiescent() ||
          g_native_capture.entry_submitted) {
        NoteNativeItemUnloadBlocked("inventory manager unavailable", manager,
                                    now);
        return;
      }
      CompleteActiveNativePreviewFailure("inventory manager unavailable");
      g_native_capture.attached_model = nullptr;
      g_native_capture.attached_scheme = -1;
      ResetNativeCapture(false);
    }
    return;
  }

  if (NativeInventoryMenuOwnsUi3D(now)) {
    // Inventory3DManager is a process-wide singleton. Once Skyrim has pushed
    // its real InventoryMenu, stop claiming the neutral manager and retire any
    // WEMU-owned transaction before the menu's next native callback. Preserve
    // the proxy cache request as pending so it resumes after the menu closes.
    ServiceNativeInventoryMenuHandoff(manager, "HUD ownership gate");
    return;
  }

  if (g_native_capture_equipment_preempt_requested.load(
          std::memory_order_acquire)) {
    if (g_deferred_ui3d_target.active ||
        g_native_capture.readback_pending ||
        g_native_capture.readback_copy_issued)
      return;
    const bool fully_idle =
        g_native_capture.stage == NativeCaptureStage::kIdle &&
        !g_native_capture.attached_model &&
        !g_native_capture.manager_lifecycle_owned &&
        !g_native_capture.entry_submitted &&
        NativeInventoryManagerNeutral(manager);
    if (fully_idle) {
      // Keep the normal prewarm queue paused until the deferred equipment
      // command consumes this quiescent hand-off on PlayerCharacter::Update.
      return;
    }
    if (g_native_capture.stage != NativeCaptureStage::kUnloading) {
      if (g_native_capture.request.form_id) {
        QueueNativePreview(g_native_capture.request.form_id,
                           g_native_capture.request.world_epoch,
                           0, 0, false, true, true);
        // This is a transparent pause, not a terminal render failure. Keep
        // the proxy cache entry pending so the priority requeue's eventual
        // real Skyrim frame is accepted. ResetNativeCapture normally emits a
        // failure for unfinished requests; suppress only that cleanup-time
        // callback after the requeue has taken ownership of completion.
        g_native_capture.completion_sent = true;
      }
      g_native_capture.stage = NativeCaptureStage::kUnloading;
      PluginLog(false,
                "[ITEM_LIFECYCLE] quiescing native item renderer before "
                "equipment mutation form=%08X requeued=%u",
                g_native_capture.request.form_id,
                g_native_capture.request.form_id ? 1u : 0u);
    }
  }

  if (g_native_capture.stage == NativeCaptureStage::kIdle) {
    // Foreground selections start on the next usable HUD frame. Background
    // cache warming deliberately leaves a gameplay window between complete
    // Skyrim model transactions, eliminating the continuous load/cull/
    // readback spikes seen when hundreds of assets were queued at world load.
    const uint64_t idle_interval_ms =
        NativePreviewQueueHeadIsPriority() ? 16 : 350;
    if (now - g_native_capture.last_finished_ms < idle_interval_ms ||
        now < g_native_capture.owner_retry_not_before_ms)
      return;
    NativePreviewRequest request = {};
    if (!PopNativePreview(&request)) return;
    const uint64_t current_epoch =
        g_world_epoch.load(std::memory_order_acquire);
    if (request.world_epoch != current_epoch ||
        !LiveWorldReadyForNativeRendering()) {
      CompleteNativePreviewFailure(request.form_id, request.world_epoch,
                                   "stale world request");
      return;
    }
    if (!g_native_item_models_enabled) {
      CompleteNativePreviewFailure(request.form_id, request.world_epoch,
                                   "native item rendering disabled");
      return;
    }
    const auto* show_inventory_3d = reinterpret_cast<const uint8_t*>(
        g_base + kShowInventory3DSetting);
    if (!RegionAllows(show_inventory_3d, sizeof(*show_inventory_3d)) ||
        ReadOr<uint8_t>(show_inventory_3d, 0) == 0) {
      CompleteNativePreviewFailure(request.form_id, request.world_epoch,
                                   "bShowInventory3D disabled");
      return;
    }
    LoadedInventoryModelRaw* models = nullptr;
    uint32_t model_count = 0;
    const bool models_valid = InventoryModels(manager, &models, &model_count);
    const int32_t manager_scheme = ReadOr<int32_t>(
        reinterpret_cast<uint8_t*>(manager) + 0x34, -1);
    void* manager_temp_reference = ReadOr<void*>(
        reinterpret_cast<uint8_t*>(manager) + 0x38, nullptr);
    // Inventory3DManager is a process-wide singleton owned by Skyrim's native
    // Inventory/Magic/Stats menus.  Only claim its neutral scheme when every
    // asynchronous/native-menu ownership signal is clear.
    if (!models_valid || InventoryLoadInFlight(manager) || model_count != 0 ||
        manager_temp_reference || manager_scheme != 0) {
      if (!request.owner_wait_started_ms)
        request.owner_wait_started_ms = now;
      ++request.owner_deferrals;
      const uint64_t owner_wait_ms =
          now >= request.owner_wait_started_ms
              ? now - request.owner_wait_started_ms
              : 0;
      if (owner_wait_ms >= kNativePreviewOwnerWaitLimitMs ||
          request.owner_deferrals >= kNativePreviewOwnerDeferralLimit) {
        PluginLog(false,
                  "item-preview: native menu ownership wait expired "
                  "form=%08X scheme=%d temp=%p task=%p models=%u "
                  "deferrals=%u elapsed=%llums",
                  request.form_id, manager_scheme, manager_temp_reference,
                  ReadOr<void*>(reinterpret_cast<uint8_t*>(manager) + 0x150,
                                nullptr),
                  model_count, request.owner_deferrals,
                  static_cast<unsigned long long>(owner_wait_ms));
        CompleteNativePreviewFailure(request.form_id, request.world_epoch,
                                     "native menu ownership timeout");
        return;
      }
      RequeueNativePreviewForOwner(request);
      g_native_capture.owner_retry_not_before_ms = now + 100;
      if (request.owner_deferrals == 1 ||
          request.owner_deferrals % 10 == 0) {
        PluginLog(false,
                  "item-preview: deferred for native menu ownership "
                  "form=%08X scheme=%d temp=%p task=%p models=%u "
                  "deferrals=%u elapsed=%llums",
                  request.form_id, manager_scheme, manager_temp_reference,
                  ReadOr<void*>(reinterpret_cast<uint8_t*>(manager) + 0x150,
                                nullptr),
                  model_count, request.owner_deferrals,
                  static_cast<unsigned long long>(owner_wait_ms));
      }
      return;
    }
    using LookupFn = void*(*)(uint32_t);
    auto lookup = reinterpret_cast<LookupFn>(g_base + kLookupForm);
    void* form = RegionAllows(reinterpret_cast<void*>(lookup), 1, true)
                     ? lookup(request.form_id)
                     : nullptr;
    const bool magic_preview = IsNativeMagicPreviewForm(form);
    // SpellItem/Shout is intentionally not a TESBoundObject. UpdateMagic3D
    // resolves its BGSMenuDisplayObject; an RTTI cast here is both unnecessary
    // and unsafe while a magic display object is still being streamed.
    void* bound_object = magic_preview ? nullptr : AsBoundObject(form);
    if (!form || (!magic_preview && !bound_object) ||
        FormType(form) == 0x35) {
      PluginLog(false,
                "[ITEM_RENDER] stale/non-renderable request rejected "
                "form=%08X epoch=%llu",
                request.form_id,
                static_cast<unsigned long long>(request.world_epoch));
      CompleteNativePreviewFailure(request.form_id, request.world_epoch,
                                   "stale/non-renderable form");
      return;
    }
    // Keep the exact object Skyrim places in its async task available until
    // the load and Clear3D have both completed. Items use the embedded native
    // InventoryEntryData; magic uses the owning SpellItem/Shout TESForm and
    // Inventory3DManager::UpdateMagic3D resolves its MDOB itself.
    g_native_capture.request = request;
    g_native_active_preview_epoch.store(request.world_epoch,
                                        std::memory_order_release);
    g_native_active_preview_form.store(request.form_id,
                                       std::memory_order_release);
    g_native_capture.started_ms = now;
    g_native_capture.capture_started_ms = 0;
    g_native_capture.next_capture_attempt_ms = 0;
    g_native_capture.unload_started_ms = 0;
    g_native_capture.last_unload_log_ms = 0;
    g_native_capture.capture_attempts = 0;
    g_native_capture.settled_frames = 0;
    g_native_capture.unload_called = false;
    g_native_capture.completion_sent = false;
    g_native_capture.magic_preview = magic_preview;
    g_native_capture.entry_submitted = false;
    g_native_capture.unload_reset_requested = false;
    g_native_capture.manager_ownership_lost = false;
    g_native_capture.owner_retry_not_before_ms = 0;
    g_native_capture.camera_fit_margin = 1.35f;
    g_native_capture.camera_pan_x = 0.0f;
    g_native_capture.camera_pan_z = 0.0f;
    g_native_capture.camera_last_distance = 0.0f;
    g_native_capture.camera_tangent_horizontal = 0.0f;
    g_native_capture.camera_tangent_vertical = 0.0f;
    g_native_capture.camera_pan_corrections = 0;
    g_native_capture.entry.object = magic_preview ? form : bound_object;
    g_native_capture.entry.extra_lists = nullptr;
    g_native_capture.entry.count_delta = 1;
    g_native_capture.entry.padding = 0;
    g_native_capture.logged_load_task = nullptr;
    g_native_capture.logged_load_state = 0xFFFFFFFFu;
    g_native_capture.logged_load_phase = 0xFFFFFFFFu;
    g_native_capture.logged_load_pending = 0xFFFFFFFFu;
    if (!AcquireNativeInventoryLifecycle(manager)) {
      CompleteActiveNativePreviewFailure("native manager initialization failed");
      ResetNativeCapture();
      return;
    }
    char form_name[160] = {};
    CopyText(form_name, sizeof(form_name), FormName(form));
    PluginLog(false,
              "[ITEM_RENDER] request accepted form=%08X name=\"%s\" "
              "type=%u form_ptr=%p bound_object=%p path=%s native_scheme=%d",
              request.form_id, form_name[0] ? form_name : "<unnamed>",
              static_cast<unsigned>(FormType(form)), form, bound_object,
              magic_preview ? "UpdateMagic3D" : "UpdateItem3D",
              ReadOr<int32_t>(reinterpret_cast<uint8_t*>(manager) + 0x34,
                              -1));
    using UpdateItemFn = void(*)(void*, NativeCaptureState::InventoryEntryDataRaw*);
    using UpdateMagicFn = void(*)(void*, void*, uint32_t);
    auto update_item = reinterpret_cast<UpdateItemFn>(
        g_base + kInventory3DUpdateItem);
    auto update_magic = reinterpret_cast<UpdateMagicFn>(
        g_base + kInventory3DUpdateMagic);
    void* update_function = magic_preview
                                ? reinterpret_cast<void*>(update_magic)
                                : reinterpret_cast<void*>(update_item);
    if (!RegionAllows(update_function, 1, true)) {
      CompleteActiveNativePreviewFailure(
          magic_preview ? "UpdateMagic3D unavailable"
                        : "UpdateItem3D unavailable");
      ResetNativeCapture();
      return;
    }
    g_native_capture.stage = NativeCaptureStage::kLoading;
    // The full native initializer must run before this call: asynchronous
    // completion reads manager+0x34 and relies on the initialized temporary
    // reference, UI3D root, lights, transform, and event registration.
    // UpdateItem3D also retains this exact embedded InventoryEntryData address
    // until Clear3D reaches an authoritative neutral manager state.
    g_native_capture.entry_submitted = true;
    if (magic_preview)
      update_magic(manager, form, 0);
    else
      update_item(manager, &g_native_capture.entry);
    LoadedInventoryModelRaw* started_models = nullptr;
    uint32_t started_model_count = 0;
    const bool started_models_valid =
        InventoryModels(manager, &started_models, &started_model_count);
    const bool load_started = ReadOr<void*>(
        reinterpret_cast<uint8_t*>(manager) + 0x150, nullptr) != nullptr;
    if (!load_started &&
        (!started_models_valid || started_model_count == 0)) {
      CompleteActiveNativePreviewFailure(
          magic_preview ? "UpdateMagic3D made no request"
                        : "UpdateItem3D made no request");
      g_native_capture.last_finished_ms = now;
      ResetNativeCapture();
      return;
    }
    PluginLog(false,
              "[ITEM_RENDER] Skyrim %s scheduled form=%08X object=%p "
              "epoch=%llu manager=%p task=%p models=%u thread=%lu",
              magic_preview ? "UpdateMagic3D" : "UpdateItem3D",
              request.form_id, g_native_capture.entry.object,
              static_cast<unsigned long long>(request.world_epoch), manager,
              ReadOr<void*>(reinterpret_cast<uint8_t*>(manager) + 0x150,
                            nullptr),
              started_model_count,
              static_cast<unsigned long>(GetCurrentThreadId()));
    LogInventoryLoadTaskTransition(manager, "scheduled");
    return;
  }

  if (g_native_capture.request.world_epoch !=
      g_world_epoch.load(std::memory_order_acquire)) {
    CompleteActiveNativePreviewFailure("active request became stale");
    g_native_capture.stage = NativeCaptureStage::kUnloading;
  }

  if (g_native_capture.manager_lifecycle_owned &&
      !g_native_capture.unload_called &&
      !NativeInventoryLifecycleMatches(manager)) {
    // A menu can win the singleton between the stack gate and this poll. This
    // is an ownership interruption, not a bad item/model: preserve the request
    // at the front of the queue so every valid inventory entry still receives
    // its real native render after the menu closes.
    RequestNativeInventoryMenuHandoff("lifecycle ownership changed");
    g_native_capture.manager_ownership_lost = true;
    g_native_capture.stage = NativeCaptureStage::kUnloading;
  }

  if (now - g_native_capture.started_ms > 10000 &&
      g_native_capture.stage == NativeCaptureStage::kCapturing) {
    PluginLog(false, "item-preview: timed out form=%08X",
              g_native_capture.request.form_id);
    CompleteActiveNativePreviewFailure("model load timed out");
    g_native_capture.stage = NativeCaptureStage::kUnloading;
  }

  if (g_native_capture.stage == NativeCaptureStage::kLoading) {
    const NativeLoadServiceResult load_result =
        ServiceCompletedNativeInventoryLoad(manager);
    if (load_result == NativeLoadServiceResult::kPending) {
      if (now - g_native_capture.started_ms <= 10000) return;
      LogInventoryLoadTaskTransition(manager, "timeout");
      PluginLog(false, "item-preview: timed out form=%08X",
                g_native_capture.request.form_id);
      CompleteActiveNativePreviewFailure("model load timed out");
      g_native_capture.stage = NativeCaptureStage::kUnloading;
    } else if (load_result == NativeLoadServiceResult::kOwnershipLost) {
      RequestNativeInventoryMenuHandoff("native load task replaced");
      g_native_capture.manager_ownership_lost = true;
      g_native_capture.stage = NativeCaptureStage::kUnloading;
    } else if (load_result == NativeLoadServiceResult::kCanceled) {
      CompleteActiveNativePreviewFailure("native model load canceled");
      g_native_capture.stage = NativeCaptureStage::kUnloading;
    } else if (load_result == NativeLoadServiceResult::kInvalid) {
      CompleteActiveNativePreviewFailure("native model completion invalid");
      g_native_capture.stage = NativeCaptureStage::kUnloading;
    }
    if (g_native_capture.stage == NativeCaptureStage::kLoading &&
        InventoryLoadInFlight(manager)) {
      CompleteActiveNativePreviewFailure(
          "native load completed with invalid model slot");
      g_native_capture.stage = NativeCaptureStage::kUnloading;
    }
    if (g_native_capture.stage != NativeCaptureStage::kLoading) return;
    void* scene_model = FindInventorySceneModel(
        manager, g_native_capture.entry.object);
    if (!scene_model) {
      CompleteActiveNativePreviewFailure(
          "native load completed without matching model");
      g_native_capture.stage = NativeCaptureStage::kUnloading;
    } else if (++g_native_capture.settled_frames < 2) {
      return;
    }
    if (g_native_capture.stage != NativeCaptureStage::kLoading) return;
    const int32_t existing_scheme = InventorySceneScheme(scene_model);
    void* scene_manager = ReadOr<void*>(
        reinterpret_cast<void*>(g_base + kUI3DSceneManagerPtr), nullptr);
    void* scheme_root = scene_manager
        ? ReadOr<void*>(reinterpret_cast<uint8_t*>(scene_manager) + 0x38 +
                            kItemPreviewScheme * sizeof(void*),
                        nullptr)
        : nullptr;
    uint16_t native_child_index = 0xFFFF;
    if (existing_scheme != kItemPreviewScheme ||
        !Ui3DRootContainsChild(scheme_root, scene_model, &native_child_index)) {
      CompleteActiveNativePreviewFailure("model attached to wrong UI3D scheme");
      g_native_capture.stage = NativeCaptureStage::kUnloading;
      return;
    }
    // Record, but never repair, native ownership. Inventory3DManager shutdown
    // owns this attachment and must be the sole path that removes it.
    g_native_capture.attached_model = scene_model;
    g_native_capture.attached_scheme = kItemPreviewScheme;
    float model_bound[4] = {};
    const bool bound_ok = scene_model &&
        Read(reinterpret_cast<uint8_t*>(scene_model) + 0xE4, &model_bound);
    PluginLog(false,
              "[ITEM_RENDER] Skyrim model ready form=%08X scene=%p "
              "parent=%p scheme=%d child_index=%u flags=%08X bound_valid=%u "
              "bound=(%.1f,%.1f,%.1f r=%.1f)",
              g_native_capture.request.form_id, scene_model,
              scene_model ? ReadOr<void*>(
                                reinterpret_cast<uint8_t*>(scene_model) + 0x30,
                                nullptr)
                          : nullptr,
              InventorySceneScheme(scene_model),
              static_cast<unsigned>(native_child_index),
              scene_model ? ReadOr<uint32_t>(
                                reinterpret_cast<uint8_t*>(scene_model) + 0xF4,
                                0)
                          : 0,
              bound_ok ? 1u : 0u, model_bound[0], model_bound[1],
              model_bound[2], model_bound[3]);
    std::atomic<uint32_t>& resource_traces = g_native_capture.magic_preview
        ? g_native_magic_resource_traces
        : g_native_item_resource_traces;
    if (resource_traces.fetch_add(1, std::memory_order_acq_rel) < 2) {
      LogSceneResources(g_native_capture.magic_preview ? "[MAGIC_RENDER]"
                                                       : "[ITEM_RENDER]",
                        g_native_capture.request.form_id, scene_model);
    }
    g_native_capture.stage = NativeCaptureStage::kCapturing;
    g_native_capture.capture_started_ms = now;
    g_native_capture.next_capture_attempt_ms = now;
    g_native_capture.capture_attempts = 0;
  }

  if (g_native_capture.stage == NativeCaptureStage::kCapturing) {
    if (g_native_capture.readback_pending) return;
    if (!g_native_capture.readback_ready &&
        now < g_native_capture.next_capture_attempt_ms)
      return;
    if (!g_native_capture.readback_ready)
      ++g_native_capture.capture_attempts;
    const bool captured = CaptureInventoryModel(chain, manager);
    // A successful native dispatch is completed by the Present hook after
    // Skyrim has drained the rest of this UI frame. Do not count it as an
    // empty attempt while that GPU result is pending.
    if (g_native_capture.readback_pending) return;
    if (captured && g_submit_item_preview) {
      g_native_capture.completion_sent = true;
      PurgeQueuedNativePreview(g_native_capture.request.form_id,
                               g_native_capture.request.world_epoch);
      g_submit_item_preview(g_native_capture.request.form_id,
                            g_native_capture.request.world_epoch,
                            dis::kNativeItemPreviewSize,
                            dis::kNativeItemPreviewSize,
                            g_native_preview_rgba,
                            dis::kNativeItemPreviewSize * 4);
      PluginLog(false,
                "[ITEM_RENDER] captured Skyrim model form=%08X "
                "samples=%u bounds=%u,%u..%u,%u output=%ux%u",
                g_native_capture.request.form_id,
                g_native_capture.content_samples,
                g_native_capture.content_min_x,
                g_native_capture.content_min_y,
                g_native_capture.content_max_x,
                g_native_capture.content_max_y,
                dis::kNativeItemPreviewSize,
                dis::kNativeItemPreviewSize);
      g_native_capture.stage = NativeCaptureStage::kUnloading;
    } else {
      const uint64_t capture_elapsed =
          now - g_native_capture.capture_started_ms;
      // Always allow the bounded terminal attempt. On slower translated
      // frames the wall-clock window can expire between the discovery and
      // correction phases; stopping there leaves a real model blank.
      if (g_native_capture.capture_attempts < kNativeCaptureMaxAttempts) {
        g_native_capture.next_capture_attempt_ms =
            now + kNativeCaptureRetrySpacingMs;
        return;
      }
      PluginLog(false,
                "[ITEM_RENDER] capture failed form=%08X scheme=%d "
                "engine_result=%u rgb=%u..%u stddev=%.1f "
                "background=%u/%u attempts=%u elapsed=%llums",
                g_native_capture.request.form_id,
                g_native_capture.rendered_scheme,
                g_native_capture.engine_render_result,
                g_native_capture.minimum_rgb,
                g_native_capture.maximum_rgb,
                g_native_capture.luminance_stddev,
                g_native_capture.background_samples,
                g_native_capture.width * g_native_capture.height,
                g_native_capture.capture_attempts,
                static_cast<unsigned long long>(capture_elapsed));
      const bool terminal_depth = g_native_capture.depth_samples >= 16;
      constexpr UINT kTerminalEdgeGuard = 4;
      const bool terminal_clip = terminal_depth && g_native_capture.width &&
          g_native_capture.height &&
          (g_native_capture.content_min_x <= kTerminalEdgeGuard ||
           g_native_capture.content_min_y <= kTerminalEdgeGuard ||
           g_native_capture.content_max_x + kTerminalEdgeGuard >=
               g_native_capture.width - 1 ||
           g_native_capture.content_max_y + kTerminalEdgeGuard >=
               g_native_capture.height - 1);
      CompleteActiveNativePreviewFailure(
          terminal_clip ? "native model remained clipped"
                        : "capture remained empty");
      g_native_capture.stage = NativeCaptureStage::kUnloading;
    }
  }

  if (g_native_capture.stage == NativeCaptureStage::kUnloading) {
    if (!g_native_capture.unload_started_ms)
      g_native_capture.unload_started_ms = now;
    if (!NativeItemReadbackQuiescent()) {
      NoteNativeItemUnloadBlocked("GPU readback drain", manager, now);
      return;
    }
    if (g_native_capture.manager_ownership_lost) {
      // A real game menu now owns the singleton.  Do not Clear3D and destroy
      // its model; only retire our local request after any old task releases
      // the InventoryEntryData pointer.
      // The old scene pointer may already have been destroyed by the menu that
      // took ownership, so do not dereference it during this recovery path.
      if (!NativeInventoryManagerNeutral(manager)) {
        NoteNativeItemUnloadBlocked("native menu owns Inventory3DManager",
                                    manager, now);
        return;
      }
      g_native_capture.attached_model = nullptr;
      g_native_capture.attached_scheme = -1;
      ClearNativeInventoryLifecycleToken();
      g_native_capture.last_finished_ms = now;
      ResetNativeCapture();
      return;
    }
    if (!g_native_capture.unload_called) {
      if (g_native_capture.manager_lifecycle_owned) {
        if (!ReleaseNativeInventoryLifecycle(manager)) {
          CompleteActiveNativePreviewFailure("native manager shutdown failed");
          NoteNativeItemUnloadBlocked("native shutdown incomplete", manager,
                                      now);
          return;
        }
        g_native_capture.attached_model = nullptr;
        g_native_capture.attached_scheme = -1;
        return;
      }
      // A prior manager replacement can clear our ownership token without
      // setting unload_called. Advance only after the replacement singleton
      // itself is neutral; otherwise this would reuse the embedded entry.
      if (g_native_capture.entry_submitted &&
          !NativeInventoryManagerNeutral(manager)) {
        NoteNativeItemUnloadBlocked("shutdown owner unavailable", manager,
                                    now);
        return;
      }
      g_native_capture.unload_called = true;
    }
    if (InventoryLoadInFlight(manager)) {
      NoteNativeItemUnloadBlocked("native load task still active", manager,
                                  now);
      return;
    }
    LoadedInventoryModelRaw* models = nullptr;
    uint32_t model_count = 0;
    if (!InventoryModels(manager, &models, &model_count) || model_count != 0) {
      NoteNativeItemUnloadBlocked("native model array not empty", manager,
                                  now);
      return;
    }
    if (!NativeInventoryManagerNeutral(manager)) {
      NoteNativeItemUnloadBlocked("native scheme/temp reference not neutral",
                                  manager, now);
      return;
    }
    g_native_capture.last_finished_ms = now;
    PluginLog(false,
              "item-preview: unloaded form=%08X elapsed=%llums queue=%u",
              g_native_capture.request.form_id,
              static_cast<unsigned long long>(now -
                                               g_native_capture.started_ms),
              g_native_preview_queue_depth.load(std::memory_order_acquire));
    ResetNativeCapture();
  }
}

void ReleaseCharacterRenderTextures() {
  if (g_character_render.readback_query)
    g_character_render.readback_query->Release();
  if (g_character_render.staging) g_character_render.staging->Release();
  if (g_character_render.depth_staging)
    g_character_render.depth_staging->Release();
  if (g_character_render.depth_view) g_character_render.depth_view->Release();
  if (g_character_render.depth_texture)
    g_character_render.depth_texture->Release();
  if (g_character_render.shader_view) g_character_render.shader_view->Release();
  if (g_character_render.render_view) g_character_render.render_view->Release();
  if (g_character_render.render_texture)
    g_character_render.render_texture->Release();
  if (g_character_render.device) g_character_render.device->Release();
  g_character_render.readback_query = nullptr;
  g_character_render.staging = nullptr;
  g_character_render.depth_staging = nullptr;
  g_character_render.depth_view = nullptr;
  g_character_render.depth_texture = nullptr;
  g_character_render.shader_view = nullptr;
  g_character_render.render_view = nullptr;
  g_character_render.render_texture = nullptr;
  g_character_render.device = nullptr;
  g_character_render.width = 0;
  g_character_render.height = 0;
  g_character_render.output_width = 0;
  g_character_render.output_height = 0;
  g_character_render.output_zoom = 1.0f;
  g_character_render.primary_aspect = 1.0f;
  g_character_render.format = DXGI_FORMAT_UNKNOWN;
  g_character_render.readback_pending = false;
  g_character_render.readback_copy_issued = false;
  g_character_render.readback_ready = false;
  g_character_render.readback_success = false;
  g_character_render.readback_dispatched_ms = 0;
  g_character_render.readback_world_epoch = 0;
  g_character_render.readback_source_revision = 0;
  g_character_render.readback_appearance_signature = 0;
  g_character_render.readback_reset_revision = 0;
  g_character_render.stable_crop_valid = false;
  g_character_inflight_signature.store(0, std::memory_order_release);
  g_character_inflight_world_epoch.store(0, std::memory_order_release);
}

bool EnsureCharacterRenderTextures(IDXGISwapChain* chain, uint32_t width,
                                   uint32_t height) {
  if (!chain || !width || !height ||
      width > dis::kCharacterPreviewMaxWidth * kCharacterCaptureScale ||
      height > dis::kCharacterPreviewMaxHeight * kCharacterCaptureScale)
    return false;
  ID3D11Device* device = nullptr;
  ID3D11Texture2D* back_buffer = nullptr;
  if (FAILED(chain->GetDevice(__uuidof(ID3D11Device),
                              reinterpret_cast<void**>(&device))) ||
      !device)
    return false;
  if (FAILED(chain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                              reinterpret_cast<void**>(&back_buffer))) ||
      !back_buffer) {
    device->Release();
    return false;
  }
  D3D11_TEXTURE2D_DESC back_desc = {};
  back_buffer->GetDesc(&back_desc);
  back_buffer->Release();
  const float primary_aspect = back_desc.Height
      ? static_cast<float>(back_desc.Width) /
            static_cast<float>(back_desc.Height)
      : 1.0f;
  if (!std::isfinite(primary_aspect) || primary_aspect < 0.5f ||
      primary_aspect > 4.0f) {
    device->Release();
    return false;
  }
  if (g_character_render.render_texture && g_character_render.readback_query &&
      g_character_render.depth_staging &&
      g_character_render.width == width &&
      g_character_render.height == height &&
      g_character_render.device == device &&
      std::abs(g_character_render.primary_aspect - primary_aspect) <
          0.0001f) {
    device->Release();
    return true;
  }

  DXGI_FORMAT format = back_desc.Format;
  switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
      break;
    default:
      format = DXGI_FORMAT_R8G8B8A8_UNORM;
      break;
  }

  ReleaseCharacterRenderTextures();
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  HRESULT result = device->CreateTexture2D(
      &desc, nullptr, &g_character_render.render_texture);
  if (SUCCEEDED(result))
    result = device->CreateRenderTargetView(
        g_character_render.render_texture, nullptr,
        &g_character_render.render_view);
  if (SUCCEEDED(result))
    result = device->CreateShaderResourceView(
        g_character_render.render_texture, nullptr,
        &g_character_render.shader_view);

  D3D11_TEXTURE2D_DESC depth_desc = {};
  depth_desc.Width = width;
  depth_desc.Height = height;
  depth_desc.MipLevels = 1;
  depth_desc.ArraySize = 1;
  depth_desc.Format = DXGI_FORMAT_R32_TYPELESS;
  depth_desc.SampleDesc.Count = 1;
  depth_desc.Usage = D3D11_USAGE_DEFAULT;
  depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
  D3D11_DEPTH_STENCIL_VIEW_DESC depth_view_desc = {};
  depth_view_desc.Format = DXGI_FORMAT_D32_FLOAT;
  depth_view_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
  depth_view_desc.Texture2D.MipSlice = 0;
  if (SUCCEEDED(result))
    result = device->CreateTexture2D(&depth_desc, nullptr,
                                     &g_character_render.depth_texture);
  if (SUCCEEDED(result))
    result = device->CreateDepthStencilView(
        g_character_render.depth_texture, &depth_view_desc,
        &g_character_render.depth_view);

  D3D11_TEXTURE2D_DESC staging_desc = desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  if (SUCCEEDED(result))
    result = device->CreateTexture2D(&staging_desc, nullptr,
                                     &g_character_render.staging);
  D3D11_TEXTURE2D_DESC depth_staging_desc = depth_desc;
  depth_staging_desc.Usage = D3D11_USAGE_STAGING;
  depth_staging_desc.BindFlags = 0;
  depth_staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  if (SUCCEEDED(result))
    result = device->CreateTexture2D(&depth_staging_desc, nullptr,
                                     &g_character_render.depth_staging);
  D3D11_QUERY_DESC query_desc = {};
  query_desc.Query = D3D11_QUERY_EVENT;
  if (SUCCEEDED(result))
    result = device->CreateQuery(&query_desc,
                                 &g_character_render.readback_query);
  if (FAILED(result)) {
    device->Release();
    PluginLog(false,
              "character-preview: off-screen texture creation failed hr=%08lX "
              "size=%ux%u format=%u",
              static_cast<unsigned long>(result), width, height,
              static_cast<unsigned>(format));
    ReleaseCharacterRenderTextures();
    return false;
  }
  // Retain the creating device so a swapchain/device replacement at the same
  // viewport size cannot accidentally reuse resources owned by the old D3D
  // device. ReleaseCharacterRenderTextures owns this reference.
  g_character_render.device = device;
  g_character_render.width = width;
  g_character_render.height = height;
  g_character_render.primary_aspect = primary_aspect;
  g_character_render.format = format;
  g_character_render.camera_fit_margin = 1.35f;
  g_character_render.camera_fit_retries = 0;
  g_character_render.camera_pan_x = 0.0f;
  g_character_render.camera_pan_z = 0.0f;
  g_character_render.camera_last_distance = 0.0f;
  g_character_render.camera_tangent_horizontal = 0.0f;
  g_character_render.camera_tangent_vertical = 0.0f;
  g_character_render.camera_pan_corrections = 0;
  g_character_render.camera_centered_confirmations = 0;
  g_character_render.camera_bounds_valid = false;
  g_character_render.stable_crop_valid = false;
  PluginLog(false,
            "character-preview: off-screen target ready size=%ux%u format=%u "
            "primary=%ux%u aspect=%.6f",
            width, height, static_cast<unsigned>(format), back_desc.Width,
            back_desc.Height, primary_aspect);
  return true;
}

bool Ui3DSchemeStackContains(void* manager, int32_t scheme,
                             uint32_t* count_out = nullptr) {
  if (count_out) *count_out = 0;
  if (!manager || !RegionAllows(manager, 0xAC)) return false;
  void* data = ReadOr<void*>(reinterpret_cast<uint8_t*>(manager) + 0x98,
                             nullptr);
  const uint32_t count = ReadOr<uint32_t>(
      reinterpret_cast<uint8_t*>(manager) + 0xA8, 0);
  if (count_out) *count_out = count;
  if (!data || count == 0 || count > 32 ||
      !RegionAllows(data, static_cast<size_t>(count) * sizeof(int32_t)))
    return false;
  for (uint32_t index = 0; index < count; ++index) {
    if (ReadOr<int32_t>(reinterpret_cast<int32_t*>(data) + index, -1) ==
        scheme)
      return true;
  }
  return false;
}

void ClearCharacterUi3DSchemeToken() {
  g_character_render.scheme_session_owned = false;
  g_character_render.scheme_manager = nullptr;
  g_character_render.scheme_root = nullptr;
}

bool AcquireCharacterUi3DScheme(void* manager) {
  if (!manager || !RegionAllows(manager, 0x118)) return false;
  void* root = ReadOr<void*>(
      reinterpret_cast<uint8_t*>(manager) + 0x38 +
          kCharacterPreviewScheme * sizeof(void*),
      nullptr);
  if (!root || !RegionAllows(root, 0xF8) || !RegionWritable(root, 0x78))
    return false;

  if (g_character_render.scheme_session_owned) {
    const bool valid = manager == g_character_render.scheme_manager &&
                       root == g_character_render.scheme_root &&
                       Ui3DSchemeStackContains(manager,
                                               kCharacterPreviewScheme);
    if (!valid) {
      PluginLog(false,
                "[CHAR_LIFECYCLE] owned scheme changed manager=%p/%p "
                "root=%p/%p; teardown skipped",
                g_character_render.scheme_manager, manager,
                g_character_render.scheme_root, root);
      ClearCharacterUi3DSchemeToken();
    }
    return valid;
  }

  uint32_t count_before = 0;
  if (Ui3DSchemeStackContains(manager, kCharacterPreviewScheme,
                              &count_before)) {
    // Skyrim 1.5.97 has no vanilla scheme-5 owner. Do not adopt or tear down a
    // scheme another mod may have placed on this process-global manager.
    PluginLog(false,
              "[CHAR_LIFECYCLE] scheme 5 already present without ownership "
              "manager=%p root=%p stack=%u",
              manager, root, count_before);
    return false;
  }

  using SchemeFn = void(*)(void*, int32_t);
  auto initialize = reinterpret_cast<SchemeFn>(
      g_base + kUI3DInitializeScheme);
  auto shutdown = reinterpret_cast<SchemeFn>(g_base + kUI3DShutdownScheme);
  if (!RegionAllows(reinterpret_cast<void*>(initialize), 1, true) ||
      !RegionAllows(reinterpret_cast<void*>(shutdown), 1, true))
    return false;

  initialize(manager, kCharacterPreviewScheme);
  uint32_t count_after = 0;
  const bool present = Ui3DSchemeStackContains(
      manager, kCharacterPreviewScheme, &count_after);
  const int32_t current = ReadOr<int32_t>(
      reinterpret_cast<uint8_t*>(manager) + 0x90, -1);
  if (!present || current != kCharacterPreviewScheme ||
      count_after != count_before + 1) {
    PluginLog(false,
              "[CHAR_LIFECYCLE] native scheme init validation failed "
              "manager=%p root=%p current=%d stack=%u->%u present=%u",
              manager, root, current, count_before, count_after,
              present ? 1u : 0u);
    if (present) shutdown(manager, kCharacterPreviewScheme);
    return false;
  }

  g_character_render.scheme_session_owned = true;
  g_character_render.scheme_manager = manager;
  g_character_render.scheme_root = root;
  PluginLog(false,
            "[CHAR_LIFECYCLE] native scheme acquired manager=%p root=%p "
            "current=%d stack=%u->%u root_local=(%.3f,%.3f,%.3f)",
            manager, root, current, count_before, count_after,
            ReadOr<float>(reinterpret_cast<uint8_t*>(root) + 0x6C, 0.0f),
            ReadOr<float>(reinterpret_cast<uint8_t*>(root) + 0x70, 0.0f),
            ReadOr<float>(reinterpret_cast<uint8_t*>(root) + 0x74, 0.0f));
  return true;
}

bool ReleaseCharacterUi3DScheme(void* manager = nullptr) {
  if (!g_character_render.scheme_session_owned) return true;
  if (!manager)
    manager = ReadOr<void*>(
        reinterpret_cast<void*>(g_base + kUI3DSceneManagerPtr), nullptr);
  void* root = manager && RegionAllows(manager, 0x118)
                   ? ReadOr<void*>(
                         reinterpret_cast<uint8_t*>(manager) + 0x38 +
                             kCharacterPreviewScheme * sizeof(void*),
                         nullptr)
                   : nullptr;
  uint32_t count_before = 0;
  const bool exact_owner = manager == g_character_render.scheme_manager &&
                           root == g_character_render.scheme_root &&
                           Ui3DSchemeStackContains(
                               manager, kCharacterPreviewScheme, &count_before);
  if (!exact_owner) {
    PluginLog(false,
              "[CHAR_LIFECYCLE] scheme ownership changed; teardown skipped "
              "manager=%p/%p root=%p/%p",
              g_character_render.scheme_manager, manager,
              g_character_render.scheme_root, root);
    ClearCharacterUi3DSchemeToken();
    return false;
  }
  using SchemeFn = void(*)(void*, int32_t);
  auto shutdown = reinterpret_cast<SchemeFn>(g_base + kUI3DShutdownScheme);
  if (!RegionAllows(reinterpret_cast<void*>(shutdown), 1, true)) return false;
  ClearCharacterUi3DSchemeToken();
  shutdown(manager, kCharacterPreviewScheme);
  uint32_t count_after = 0;
  const bool remains = Ui3DSchemeStackContains(
      manager, kCharacterPreviewScheme, &count_after);
  if (remains) {
    // Shutdown did not remove our exact stack entry. Restore the ownership
    // token so a later render-thread pass can retry instead of leaking an
    // unowned scheme which AcquireCharacterUi3DScheme must reject forever.
    void* current_root = ReadOr<void*>(
        reinterpret_cast<uint8_t*>(manager) + 0x38 +
            kCharacterPreviewScheme * sizeof(void*),
        nullptr);
    if (current_root == root) {
      g_character_render.scheme_session_owned = true;
      g_character_render.scheme_manager = manager;
      g_character_render.scheme_root = root;
    }
  }
  PluginLog(false,
            "[CHAR_LIFECYCLE] native scheme released manager=%p root=%p "
            "stack=%u->%u current=%d remains=%u",
            manager, root, count_before, count_after,
            ReadOr<int32_t>(reinterpret_cast<uint8_t*>(manager) + 0x90, -1),
            remains ? 1u : 0u);
  return !remains;
}

bool PrepareCharacterCloneForRelease(void* root) {
  if (!root || !RegionAllows(root, 0x110)) return false;
  constexpr uint32_t kCapacity = 512;
  void* queue[kCapacity] = {};
  uint32_t read_index = 0;
  uint32_t queue_count = 1;
  uint32_t repaired_trees = 0;
  queue[0] = root;
  while (read_index < queue_count && read_index < kCapacity) {
    void* object = queue[read_index];
    ++read_index;
    if (!object || !RegionAllows(object, 0x110)) continue;
    void** vtable = ReadOr<void**>(object, nullptr);
    if (vtable == reinterpret_cast<void**>(
                      g_base + kBSFlattenedBoneTreeVtable)) {
      const int32_t bone_count = ReadOr<int32_t>(
          reinterpret_cast<uint8_t*>(object) + 0x128, -1);
      void* bone_records = ReadOr<void*>(
          reinterpret_cast<uint8_t*>(object) + 0x130, nullptr);
      if (bone_count == 0 && !bone_records) {
        ++repaired_trees;
      } else {
        constexpr int32_t kMaximumFlattenedBones = 1024;
        if (bone_count <= 0 || bone_count > kMaximumFlattenedBones ||
            !bone_records) {
          PluginLog(false,
                    "[CHAR_RELEASE_FIX] rejected invalid BSFlattenedBoneTree "
                    "tree=%p count=%d records=%p",
                    object, bone_count, bone_records);
          return false;
        }
        const size_t records_size =
            static_cast<size_t>(bone_count) * 0x80u;
        uint8_t* allocation_base =
            reinterpret_cast<uint8_t*>(bone_records) - sizeof(uint64_t);
        uint64_t allocation_cookie = 0;
        if (!RegionWritable(allocation_base,
                            records_size + sizeof(uint64_t)) ||
            !Read(allocation_base, &allocation_cookie) ||
            allocation_cookie != static_cast<uint64_t>(bone_count)) {
          PluginLog(false,
                    "[CHAR_RELEASE_FIX] rejected unverified cloned bone "
                    "array tree=%p count=%d records=%p base=%p cookie=%llu",
                    object, bone_count, bone_records, allocation_base,
                    static_cast<unsigned long long>(allocation_cookie));
          return false;
        }
        using ArrayDeleteFn = void*(*)(void*, uint32_t);
        auto delete_array = reinterpret_cast<ArrayDeleteFn>(
            g_base + kBSFlattenedBoneRecordArrayDelete);
        if (!RegionAllows(reinterpret_cast<void*>(delete_array), 1, true)) {
          PluginLog(false,
                    "[CHAR_RELEASE_FIX] bone array destructor unavailable");
          return false;
        }

        // Flag 2 selects the compiler-emitted array path (which reads the
        // count cookie and destroys every 0x80-byte record); flag 1 frees the
        // true allocation base at records-8. This is the exact path emitted by
        // Skyrim for this record type, not an approximation of its ownership.
        delete_array(bone_records, 3u);
        const int32_t empty_count = 0;
        void* no_records = nullptr;
        if (!WriteValue(reinterpret_cast<uint8_t*>(object) + 0x128,
                        empty_count) ||
            !WriteValue(reinterpret_cast<uint8_t*>(object) + 0x130,
                        no_records)) {
          PluginLog(false,
                    "[CHAR_RELEASE_FIX] failed to neutralize repaired tree=%p",
                    object);
          return false;
        }
        ++repaired_trees;
        PluginLog(false,
                  "[CHAR_RELEASE_FIX] retired clone-only bone array tree=%p "
                  "count=%d records=%p allocation=%p",
                  object, bone_count, bone_records, allocation_base);
      }
    }
    void* node = SceneVirtualPointer(object, 3);
    void** children = node ? ReadOr<void**>(
        reinterpret_cast<uint8_t*>(node) + 0x118, nullptr) : nullptr;
    uint16_t capacity = node ? ReadOr<uint16_t>(
        reinterpret_cast<uint8_t*>(node) + 0x120, 0) : 0;
    capacity = std::min<uint16_t>(capacity, kCapacity);
    for (uint16_t child_index = 0;
         children && child_index < capacity && queue_count < kCapacity;
         ++child_index) {
      void* child = ReadOr<void*>(children + child_index, nullptr);
      if (!child || !RegionAllows(child, sizeof(void*))) continue;
      bool duplicate = false;
      for (uint32_t prior = 0; prior < queue_count; ++prior) {
        if (queue[prior] == child) {
          duplicate = true;
          break;
        }
      }
      if (!duplicate) queue[queue_count++] = child;
    }
  }
  PluginLog(false,
            "[CHAR_RELEASE_FIX] prepared actor clone root=%p objects=%u "
            "flattened_trees=%u truncated=%u",
            root, read_index, repaired_trees,
            queue_count >= kCapacity ? 1u : 0u);
  return queue_count < kCapacity;
}

void ReleasePreparedCharacterClone(void* clone, const char* reason) {
  if (!clone) return;
  PluginLog(false,
            "[CHAR_CRASH_STAGE] release begin clone=%p ref=%ld reason=%s",
            clone, static_cast<long>(ReadOr<LONG>(
                       reinterpret_cast<uint8_t*>(clone) + 8, -1)),
            reason ? reason : "unspecified");
  g_character_clone_release_object.store(clone, std::memory_order_release);
  g_character_clone_release_thread.store(GetCurrentThreadId(),
                                           std::memory_order_release);
  g_character_clone_release_probe.store(true, std::memory_order_release);
  ReleaseNiObject(clone);
  g_character_clone_release_probe.store(false, std::memory_order_release);
  g_character_clone_release_thread.store(0, std::memory_order_release);
  g_character_clone_release_object.store(nullptr, std::memory_order_release);
  PluginLog(false,
            "[CHAR_CRASH_STAGE] release complete clone=%p reason=%s",
            clone, reason ? reason : "unspecified");
}

bool ReleaseCharacterCloneSafely(void* clone, const char* reason) {
  if (!clone) return true;
  if (!PrepareCharacterCloneForRelease(clone)) {
    PluginLog(false,
              "[CHAR_RELEASE_FIX] refused unsafe character clone release "
              "clone=%p reason=%s",
              clone, reason ? reason : "unspecified");
    return false;
  }
  ReleasePreparedCharacterClone(clone, reason);
  return true;
}

// A drag gesture submits several orientations of the same frozen Skyrim
// actor. Detach that actor after each ordered GPU event but retain WEMU's
// strong reference until the gesture ends. Re-cloning the live actor for every
// touch revision changed pose/bounds underneath the camera and was measured in
// the device log as an alternating 78px/180px-wide silhouette.
bool DetachActiveCharacterForInteraction(void* manager) {
  if (!g_character_render.active_clone) return true;
  if (!CharacterCloneLifecycleQuiescent()) return false;
  void* clone = g_character_render.active_clone;
  void* actual_parent = ReadOr<void*>(
      reinterpret_cast<uint8_t*>(clone) + 0x30, nullptr);
  void* scheme_root = manager
                          ? ReadOr<void*>(
                                reinterpret_cast<uint8_t*>(manager) +
                                    0x38 + kCharacterPreviewScheme *
                                               sizeof(void*),
                                nullptr)
                          : nullptr;
  if (actual_parent && g_character_render.attached && manager &&
      RegionAllows(manager, 0x118) && scheme_root &&
      RegionAllows(scheme_root, sizeof(void*)) &&
      actual_parent == scheme_root) {
    using DetachFn = void(*)(void*, void*, int32_t);
    auto detach = reinterpret_cast<DetachFn>(
        g_base + kUI3DDetachChildScheme);
    if (RegionAllows(reinterpret_cast<void*>(detach), 1, true))
      detach(manager, clone, kCharacterPreviewScheme);
  }
  actual_parent = ReadOr<void*>(reinterpret_cast<uint8_t*>(clone) + 0x30,
                                nullptr);
  if (actual_parent && RegionAllows(actual_parent, sizeof(void*))) {
    void** parent_vtable = ReadOr<void**>(actual_parent, nullptr);
    void* raw_detach =
        parent_vtable ? ReadOr<void*>(parent_vtable + 0x38, nullptr) : nullptr;
    if (RegionAllows(raw_detach, 1, true))
      reinterpret_cast<void(*)(void*, void*)>(raw_detach)(actual_parent,
                                                           clone);
  }
  actual_parent = ReadOr<void*>(reinterpret_cast<uint8_t*>(clone) + 0x30,
                                nullptr);
  if (actual_parent) {
    PluginLog(false,
              "[CHAR_INTERACTION] retained clone detach failed clone=%p "
              "parent=%p root=%p",
              clone, actual_parent, scheme_root);
    return false;
  }
  g_character_render.attached = false;
  g_character_render.attached_root = nullptr;
  g_character_render.submitted_scene_pending_retirement = false;
  PluginLog(false,
            "[CHAR_INTERACTION] retained frozen clone=%p revision=%llu",
            clone, static_cast<unsigned long long>(
                       g_character_render.last_control_revision));
  return true;
}

bool DetachActiveCharacter(void* manager) {
  if (!g_character_render.active_clone) return true;
  if (!CharacterCloneLifecycleQuiescent()) {
    PluginLog(false,
              "[CHAR_CRASH_STAGE] detach deferred lease=%u owner=%s "
              "readback=%u copy=%u clone=%p",
              g_deferred_ui3d_target.active ? 1u : 0u,
              DeferredUi3DTargetOwnerName(g_deferred_ui3d_target.owner),
              g_character_render.readback_pending ? 1u : 0u,
              g_character_render.readback_copy_issued ? 1u : 0u,
              g_character_render.active_clone);
    return false;
  }
  void* clone = g_character_render.active_clone;
  const LONG refcount_before = ReadOr<LONG>(
      reinterpret_cast<uint8_t*>(clone) + 8, -1);
  void* actual_parent = ReadOr<void*>(
      reinterpret_cast<uint8_t*>(clone) + 0x30, nullptr);
  void* scheme_root = manager
                          ? ReadOr<void*>(
                                reinterpret_cast<uint8_t*>(manager) +
                                    0x38 + kCharacterPreviewScheme *
                                               sizeof(void*),
                                nullptr)
                          : nullptr;
  PluginLog(false,
            "[CHAR_CRASH_STAGE] detach begin clone=%p parent=%p "
            "expected_root=%p attached=%u ref=%ld",
            clone, actual_parent, g_character_render.attached_root,
            g_character_render.attached ? 1u : 0u,
            static_cast<long>(refcount_before));
  if (actual_parent && g_character_render.attached && manager &&
      RegionAllows(manager, 0x118) && scheme_root &&
      RegionAllows(scheme_root, sizeof(void*)) &&
      actual_parent == scheme_root &&
      (!g_character_render.attached_root ||
       g_character_render.attached_root == scheme_root)) {
    using DetachFn = void(*)(void*, void*, int32_t);
    auto detach = reinterpret_cast<DetachFn>(
        g_base + kUI3DDetachChildScheme);
    if (RegionAllows(reinterpret_cast<void*>(detach), 1, true)) {
      detach(manager, clone, kCharacterPreviewScheme);
      PluginLog(false,
                "[CHAR_CRASH_STAGE] native detach returned clone=%p",
                clone);
    }
  }

  actual_parent = ReadOr<void*>(reinterpret_cast<uint8_t*>(clone) + 0x30,
                                nullptr);
  if (actual_parent && RegionAllows(actual_parent, sizeof(void*))) {
    // If UI3D changed roots or the manager is already being rebuilt, detach
    // through the clone's real NiNode parent instead of releasing a child that
    // is still referenced by an unknown scene. NiNode::DetachChild2 is slot
    // 0x38 in Skyrim SE 1.5.97.
    void** parent_vtable = ReadOr<void**>(actual_parent, nullptr);
    void* raw_detach =
        parent_vtable ? ReadOr<void*>(parent_vtable + 0x38, nullptr) : nullptr;
    if (RegionAllows(raw_detach, 1, true)) {
      using ParentDetachFn = void(*)(void*, void*);
      reinterpret_cast<ParentDetachFn>(raw_detach)(actual_parent, clone);
      PluginLog(false,
                "[CHAR_CRASH_STAGE] parent detach returned clone=%p "
                "parent=%p",
                clone, actual_parent);
    }
  }

  actual_parent = ReadOr<void*>(reinterpret_cast<uint8_t*>(clone) + 0x30,
                                nullptr);
  if (actual_parent) {
    const uint64_t now = GetTickCount64();
    if (now - g_character_render.last_failure_log_ms >= 5000) {
      PluginLog(false,
                "character-preview: refusing to release attached clone=%p "
                "parent=%p expected_root=%p",
                clone, actual_parent, g_character_render.attached_root);
      g_character_render.last_failure_log_ms = now;
    }
    return false;
  }
  if (!PrepareCharacterCloneForRelease(clone)) {
    // Keep our strong reference and retry on the next quiescent Present. Never
    // enter Skyrim's known-invalid BSFlattenedBoneTree clone destructor with
    // an array that did not pass all cookie, bounds, and vtable checks.
    g_character_render.attached = false;
    g_character_render.attached_root = nullptr;
    PluginLog(false,
              "[CHAR_RELEASE_FIX] clone release deferred after validation "
              "failure clone=%p",
              clone);
    return false;
  }
  // Remove every globally discoverable ownership token before destruction.
  // A destructor callback can therefore never find and release the same clone
  // through the character-preview state a second time.
  g_character_render.attached = false;
  g_character_render.attached_root = nullptr;
  g_character_render.active_clone = nullptr;
  g_character_render.submitted_scene_pending_retirement = false;
  g_character_render.active_world_epoch = 0;
  g_character_render.active_source_revision = 0;
  g_character_adopted_signature.store(0, std::memory_order_release);
  ReleasePreparedCharacterClone(clone, "active scene retirement");
  return true;
}

bool RetireCharacterSceneObjects(void* manager, const char* reason,
                                 bool release_scheme) {
  if (!CharacterCloneLifecycleQuiescent()) return false;
  const bool had_active = g_character_render.active_clone != nullptr;
  if (had_active &&
      (!manager || !RegionAllows(manager, 0x118) ||
       !DetachActiveCharacter(manager)))
    return false;

  void* pending = nullptr;
  AcquireSRWLockExclusive(&g_pending_character_lock);
  pending = g_pending_character.clone;
  g_pending_character = {};
  ReleaseSRWLockExclusive(&g_pending_character_lock);
  if (pending) {
    PluginLog(false,
              "[CHAR_CRASH_STAGE] pending release begin clone=%p ref=%ld "
              "reason=%s",
              pending, static_cast<long>(ReadOr<LONG>(
                           reinterpret_cast<uint8_t*>(pending) + 8, -1)),
              reason ? reason : "unspecified");
    ReleaseCharacterCloneSafely(pending, reason ? reason
                                                : "pending scene retirement");
    PluginLog(false,
              "[CHAR_CRASH_STAGE] pending release complete clone=%p "
              "reason=%s",
              pending, reason ? reason : "unspecified");
  }
  if (release_scheme) {
    const bool released = ReleaseCharacterUi3DScheme(manager);
    if (!released && g_character_render.scheme_session_owned) return false;
  }
  if (had_active || pending) {
    PluginLog(false,
              "[CHAR_LIFECYCLE] transient scene retired reason=%s "
              "active=%u pending=%p cached=%u",
              reason ? reason : "unspecified", had_active ? 1u : 0u,
              pending, g_character_render.has_good_frame ? 1u : 0u);
  }
  return true;
}

bool RetireSubmittedCharacterSceneAtPresent() {
  // Restoring the framebuffer lease only removes WEMU's global renderer-state
  // publication. It does not complete the actor draw. ServiceCharacterPresent-
  // Readback inserts an event after the draw and both ordered texture copies;
  // keep the shallow actor graph alive until that event has completed and the
  // staging resources have been mapped. Deliberately leave a concurrently
  // published replacement in the pending slot for the next transaction.
  if (!g_character_render.submitted_scene_pending_retirement) return true;
  void* manager = ReadOr<void*>(
      reinterpret_cast<void*>(g_base + kUI3DSceneManagerPtr), nullptr);
  if (!g_character_render.active_clone) {
    const bool released = ReleaseCharacterUi3DScheme(manager);
    if (!released && g_character_render.scheme_session_owned) return false;
    g_character_render.submitted_scene_pending_retirement = false;
    return released;
  }
  if (!CharacterCloneLifecycleQuiescent()) return false;
  const bool keep_for_interaction =
      g_character_interacting.load(std::memory_order_acquire) &&
      g_character_render.active_world_epoch ==
          g_world_epoch.load(std::memory_order_acquire) &&
      !g_character_transition_pending.load(std::memory_order_acquire) &&
      LiveWorldReadyForNativeRendering();
  const bool detached = manager && RegionAllows(manager, 0x118) &&
      (keep_for_interaction
           ? DetachActiveCharacterForInteraction(manager)
           : DetachActiveCharacter(manager));
  if (!detached) {
    PluginLog(false,
              "[CHAR_CRASH_STAGE] submitted-scene Present retirement "
              "deferred clone=%p manager=%p lease=%u retained=%u",
              g_character_render.active_clone, manager,
              g_deferred_ui3d_target.active ? 1u : 0u,
              keep_for_interaction ? 1u : 0u);
    return false;
  }
  if (!ReleaseCharacterUi3DScheme(manager) &&
      g_character_render.scheme_session_owned) {
    g_character_render.submitted_scene_pending_retirement = true;
    PluginLog(false,
              "[CHAR_CRASH_STAGE] submitted-scene scheme release deferred "
              "manager=%p",
              manager);
    return false;
  }
  PluginLog(false,
            "[CHAR_LIFECYCLE] submitted actor scene retired after GPU event; "
            "pending=%u copy=%u ready=%u retained=%u",
            g_character_render.readback_pending ? 1u : 0u,
            g_character_render.readback_copy_issued ? 1u : 0u,
            g_character_render.readback_ready ? 1u : 0u,
            keep_for_interaction ? 1u : 0u);
  return true;
}


void* PendingCharacterPointer() {
  AcquireSRWLockShared(&g_pending_character_lock);
  void* clone = g_pending_character.clone;
  ReleaseSRWLockShared(&g_pending_character_lock);
  return clone;
}

void RequeuePendingCharacter(PendingCharacterClone pending) {
  if (!pending.clone) return;
  bool requeued = false;
  AcquireSRWLockExclusive(&g_pending_character_lock);
  if (pending.world_epoch == g_world_epoch.load(std::memory_order_acquire) &&
      !g_pending_character.clone) {
    g_pending_character = pending;
    requeued = true;
  }
  ReleaseSRWLockExclusive(&g_pending_character_lock);
  if (!requeued)
    ReleaseCharacterCloneSafely(pending.clone, "pending requeue collision");
}

bool ReattachInteractiveCharacter(void* manager) {
  void* clone = g_character_render.active_clone;
  if (!clone || g_character_render.attached || !manager ||
      !RegionAllows(manager, 0x118))
    return false;
  if (!AcquireCharacterUi3DScheme(manager)) return false;
  void* scheme_root = ReadOr<void*>(
      reinterpret_cast<uint8_t*>(manager) +
          0x38 + kCharacterPreviewScheme * sizeof(void*),
      nullptr);
  using AttachFn = void(*)(void*, void*, int32_t);
  auto attach = reinterpret_cast<AttachFn>(g_base + kUI3DAttachChild);
  if (!scheme_root || !RegionAllows(scheme_root, sizeof(void*)) ||
      !RegionAllows(reinterpret_cast<void*>(attach), 1, true))
    return false;
  if (ReadOr<void*>(reinterpret_cast<uint8_t*>(clone) + 0x30, nullptr))
    return false;
  attach(manager, clone, kCharacterPreviewScheme);
  void* parent = ReadOr<void*>(reinterpret_cast<uint8_t*>(clone) + 0x30,
                               nullptr);
  uint16_t child_index = 0xFFFF;
  if (parent != scheme_root ||
      !Ui3DRootContainsChild(scheme_root, clone, &child_index)) {
    PluginLog(false,
              "[CHAR_INTERACTION] retained clone reattach failed clone=%p "
              "parent=%p root=%p index=%u",
              clone, parent, scheme_root,
              static_cast<unsigned>(child_index));
    return false;
  }
  g_character_render.attached = true;
  g_character_render.attached_root = scheme_root;
  PluginLog(false,
            "[CHAR_INTERACTION] reattached frozen clone=%p revision=%llu",
            clone, static_cast<unsigned long long>(
                       g_character_render.last_control_revision));
  return true;
}

bool AdoptPendingCharacter(void* manager) {
  if (!CharacterCloneLifecycleQuiescent()) return false;
  const uint64_t reset_revision =
      g_character_reset_revision.load(std::memory_order_acquire);
  if (reset_revision != g_character_render.observed_reset_revision) {
    if (!DetachActiveCharacter(manager)) return false;
    g_character_render.observed_reset_revision = reset_revision;
  }
  const uint64_t world_epoch = g_world_epoch.load(std::memory_order_acquire);
  if (g_character_render.active_clone && !g_character_render.attached) {
    const bool can_reuse =
        g_character_interacting.load(std::memory_order_acquire) &&
        g_character_render.active_world_epoch == world_epoch &&
        !PendingCharacterPointer() && LiveWorldReadyForNativeRendering();
    if (can_reuse) {
      if (!ReattachInteractiveCharacter(manager)) return false;
    } else {
      if (!DetachActiveCharacter(manager)) return false;
      // A non-interactive failed clone was the publication for this
      // appearance. Ask the player-update thread for one replacement only
      // when no verified cached portrait can continue serving the panel.
      if (!g_character_render.has_good_frame)
        g_character_clone_retry_requested.store(true,
                                                  std::memory_order_release);
    }
  }
  if (g_character_render.active_clone &&
      g_character_render.active_world_epoch != world_epoch) {
    if (!DetachActiveCharacter(manager)) return false;
  }

  PendingCharacterClone pending = {};
  AcquireSRWLockExclusive(&g_pending_character_lock);
  pending = g_pending_character;
  g_pending_character = {};
  ReleaseSRWLockExclusive(&g_pending_character_lock);
  if (!pending.clone)
    return g_character_render.active_clone != nullptr &&
           g_character_render.attached;
  if (pending.world_epoch != world_epoch) {
    ReleaseCharacterCloneSafely(pending.clone, "stale pending epoch");
    return g_character_render.active_clone != nullptr &&
           g_character_render.attached;
  }
  PluginLog(false,
            "[CHAR_CRASH_STAGE] adopt begin pending=%p revision=%llu "
            "signature=%016llX active=%p",
            pending.clone,
            static_cast<unsigned long long>(pending.source_revision),
            static_cast<unsigned long long>(pending.appearance_signature),
            g_character_render.active_clone);
  void* scheme_root = manager
                          ? ReadOr<void*>(
                                reinterpret_cast<uint8_t*>(manager) +
                                    0x38 + kCharacterPreviewScheme *
                                               sizeof(void*),
                                nullptr)
                          : nullptr;
  if (!manager || !RegionAllows(manager, 0x118) || !scheme_root ||
      !RegionAllows(scheme_root, sizeof(void*))) {
    RequeuePendingCharacter(pending);
    return false;
  }
  if (!AcquireCharacterUi3DScheme(manager)) {
    RequeuePendingCharacter(pending);
    return false;
  }
  using AttachFn = void(*)(void*, void*, int32_t);
  auto attach = reinterpret_cast<AttachFn>(g_base + kUI3DAttachChild);
  if (!RegionAllows(reinterpret_cast<void*>(attach), 1, true)) {
    RequeuePendingCharacter(pending);
    return false;
  }
  // Save teardown can begin after the pending record is removed from its lock.
  // Never attach a clone after its source world has stopped being live.
  if (pending.world_epoch !=
          g_world_epoch.load(std::memory_order_acquire) ||
      reset_revision !=
          g_character_reset_revision.load(std::memory_order_acquire) ||
      !LiveWorldReadyForNativeRendering()) {
    RequeuePendingCharacter(pending);
    return false;
  }
  // Keep the known-good preview attached until every prerequisite for the
  // replacement has been validated. This makes appearance swaps
  // transactional with respect to renderer/scheme teardown and prevents a
  // transient manager rebuild from discarding the last valid actor.
  PluginLog(false,
            "[CHAR_CRASH_STAGE] adopt retire-old begin active=%p",
            g_character_render.active_clone);
  if (!DetachActiveCharacter(manager)) {
    RequeuePendingCharacter(pending);
    return false;
  }
  PluginLog(false,
            "[CHAR_CRASH_STAGE] adopt retire-old complete pending=%p",
            pending.clone);
  const bool retain_cached_frame =
      g_character_render.has_good_frame &&
      g_character_render.cached_world_epoch == pending.world_epoch;
  const bool retain_transition_framing =
      retain_cached_frame &&
      GetTickCount64() < g_character_live_transition_until_ms.load(
                             std::memory_order_acquire);
  if (!retain_transition_framing) {
    std::copy(std::begin(pending.source_center),
              std::end(pending.source_center),
              std::begin(g_character_render.source_center));
    g_character_render.source_radius = pending.source_radius;
  }
  g_character_render.scene_objects = pending.scene_objects;
  g_character_render.bounded_objects = pending.bounded_objects;
  g_character_render.geometry_objects = pending.geometry_objects;
  PluginLog(false,
            "[CHAR_CRASH_STAGE] attach begin pending=%p root=%p",
            pending.clone, scheme_root);
  attach(manager, pending.clone, kCharacterPreviewScheme);
  void* attached_parent = ReadOr<void*>(
      reinterpret_cast<uint8_t*>(pending.clone) + 0x30, nullptr);
  PluginLog(false,
            "[CHAR_CRASH_STAGE] attach returned pending=%p parent=%p root=%p",
            pending.clone, attached_parent, scheme_root);
  uint16_t native_child_index = 0xFFFF;
  const bool attached_in_scene =
      attached_parent == scheme_root &&
      Ui3DRootContainsChild(scheme_root, pending.clone, &native_child_index);
  if (!attached_in_scene) {
    if (attached_parent && RegionAllows(attached_parent, sizeof(void*))) {
      void** parent_vtable = ReadOr<void**>(attached_parent, nullptr);
      void* raw_detach =
          parent_vtable ? ReadOr<void*>(parent_vtable + 0x38, nullptr) : nullptr;
      if (RegionAllows(raw_detach, 1, true))
        reinterpret_cast<void(*)(void*, void*)>(raw_detach)(attached_parent,
                                                            pending.clone);
    }
    void* remaining_parent = ReadOr<void*>(
        reinterpret_cast<uint8_t*>(pending.clone) + 0x30, nullptr);
    if (!remaining_parent) {
      RequeuePendingCharacter(pending);
    } else {
      // Preserve our reference until a later render-thread pass can safely
      // detach from the actual parent. Releasing here would leave the scene
      // graph holding a potentially dangling child.
      g_character_render.active_clone = pending.clone;
      g_character_render.attached = false;
      g_character_render.attached_root = remaining_parent;
      g_character_render.active_world_epoch = pending.world_epoch;
      g_character_adopted_signature.store(pending.appearance_signature,
                                          std::memory_order_release);
    }
    const uint64_t now = GetTickCount64();
    if (now - g_character_render.last_failure_log_ms >= 5000) {
      PluginLog(false,
                "character-preview: UI3D attach verification failed clone=%p "
                "parent=%p expected_root=%p child_membership=%u index=%u",
                pending.clone, attached_parent, scheme_root,
                attached_in_scene ? 1u : 0u,
                static_cast<unsigned>(native_child_index));
      g_character_render.last_failure_log_ms = now;
    }
    return false;
  }
  g_character_render.active_clone = pending.clone;
  g_character_render.attached = true;
  g_character_render.attached_root = scheme_root;
  g_character_render.active_world_epoch = pending.world_epoch;
  g_character_render.active_source_revision =
      ((pending.world_epoch & 0xFFFFFFFFull) << 32) |
      (pending.source_revision & 0xFFFFFFFFull);
  g_character_adopted_signature.store(pending.appearance_signature,
                                      std::memory_order_release);
  g_character_render.last_control_revision = 0;
  g_character_render.pose_initialized = false;
  // Adoption begins a replacement transaction; it does not invalidate the
  // durable pixels already owned by the bridge. Keep serving that frame until
  // Present publishes this clone successfully. A failed transitional clone
  // therefore cannot turn the panel blank.
  if (!retain_cached_frame) {
    g_character_render.has_good_frame = false;
    g_character_render.cached_world_epoch = 0;
    g_character_render.cached_source_revision = 0;
    g_character_render.cached_appearance_signature = 0;
    g_character_cached_signature.store(0, std::memory_order_release);
    g_character_cached_world_epoch.store(0, std::memory_order_release);
  }
  // Leave deliberate head/foot and equipment room in the portrait. The old
  // 1.60 fit filled the panel and made armor/weapons look oversized even when
  // technically inside the render target.
  g_character_render.camera_fit_margin = 1.35f;
  g_character_render.camera_fit_retries = 0;
  if (!retain_transition_framing) {
    g_character_render.camera_pan_x = 0.0f;
    g_character_render.camera_pan_z = 0.0f;
    g_character_render.camera_last_distance = 0.0f;
    g_character_render.camera_tangent_horizontal = 0.0f;
    g_character_render.camera_tangent_vertical = 0.0f;
    g_character_render.camera_pan_corrections = 0;
    g_character_render.camera_centered_confirmations = 0;
    g_character_render.camera_bounds_valid = false;
    g_character_render.camera_bounds_control_revision = 0;
    g_character_render.camera_bounds_visited = 0;
    g_character_render.camera_bounds_geometry = 0;
    g_character_render.stable_crop_valid = false;
  }
  g_character_render.consecutive_empty_frames = 0;
  g_character_render.empty_render_suspended = false;
  PluginLog(false,
            "character-preview: live clone attached clone=%p radius=%.1f "
            "center=(%.1f,%.1f,%.1f) revision=%llu",
            pending.clone, g_character_render.source_radius,
            g_character_render.source_center[0],
            g_character_render.source_center[1],
            g_character_render.source_center[2],
            static_cast<unsigned long long>(
                g_character_render.active_source_revision));
  return true;
}

bool UpdateCharacterTransform(const dis::CharacterPreviewSurface& control) {
  void* root = g_character_render.active_clone;
  if (!root || !RegionAllows(root, 0x110))
    return false;
  // NiAVObject::Clone has already run Skyrim's CreateClone + ProcessClone
  // transaction on the post-Actor::Update tree. In particular, the cloned
  // BSFlattenedBoneTree and every NiSkinInstance contain an evaluated palette
  // in the actor's original world space. Moving or rotating the clone root and
  // then calling the generic NiAVObject update does not rebuild that actor-only
  // flattened palette. Rigid attachments follow the new root while skinned
  // vertices continue to use the copied palette, producing the long triangles
  // seen in the device capture.
  //
  // Preserve Skyrim's complete evaluated clone verbatim. The preview camera
  // orbits the actor below, so rotation and zoom never write scene state and
  // cannot deform the skin or fight the gameplay camera. Because no controller
  // update is run on this detached clone, the accepted post-update standing
  // pose is also naturally frozen.
  if (g_character_render.pose_initialized &&
      g_character_render.last_control_revision == control.control_revision)
    return true;
  if (!g_character_render.pose_initialized) {
    SceneTransform world = {};
    float bound[4] = {};
    const bool world_valid = ReadSceneTransform(root, &world);
    const bool bound_valid =
        Read(reinterpret_cast<uint8_t*>(root) + 0xE4, &bound) &&
        std::isfinite(bound[0]) && std::isfinite(bound[1]) &&
        std::isfinite(bound[2]) && std::isfinite(bound[3]) &&
        bound[3] > 0.01f;
    PluginLog(false,
              "[CHAR_PREVIEW] preserved engine-evaluated frozen pose root=%p "
              "world=(%.1f,%.1f,%.1f s=%.3f) "
              "bound=(%.1f,%.1f,%.1f r=%.1f) valid=%u%u "
              "control_revision=%llu",
              root, world.translate[0], world.translate[1],
              world.translate[2], world.scale, bound[0], bound[1], bound[2],
              bound[3], world_valid ? 1u : 0u, bound_valid ? 1u : 0u,
              static_cast<unsigned long long>(control.control_revision));
  }
  g_character_render.last_control_revision = control.control_revision;
  g_character_render.pose_initialized = true;
  return true;
}

bool ReframeCharacterCapture(uint32_t min_x, uint32_t min_y,
                             uint32_t max_x, uint32_t max_y) {
  const uint32_t source_width = g_character_render.width;
  const uint32_t source_height = g_character_render.height;
  const uint32_t output_width = g_character_render.output_width;
  const uint32_t output_height = g_character_render.output_height;
  if (!source_width || !source_height || !output_width || !output_height ||
      output_width > dis::kCharacterPreviewMaxWidth ||
      output_height > dis::kCharacterPreviewMaxHeight ||
      min_x > max_x || min_y > max_y || max_x >= source_width ||
      max_y >= source_height)
    return false;

  // The depth silhouette changes shape at every yaw (especially with a bow,
  // shield, or staff). Cropping to those per-frame extrema made the actor's
  // scale and center jump even though the native camera was steady. Derive a
  // single padded crop from the first authoritative depth silhouette and
  // retain it for this appearance clone.
  //
  // Do not assume that the UI3D projection axis is the physical center of the
  // private render target. Skyrim's menu render applies its own post-camera
  // viewport transform: live 1.5.97 captures place the actor near y=350 in a
  // 1365-pixel target. The old source_height/2 anchor consequently produced a
  // crop beginning near y=420, below the actor's last covered pixel, and then
  // published an entirely transparent portrait despite a valid GPU draw.
  // The copied depth buffer is the authoritative location of the actual actor
  // draw, so lock the crop to its measured center instead.
  if (!g_character_render.stable_crop_valid) {
    const float output_aspect =
        static_cast<float>(output_width) / std::max(1u, output_height);
    const float measured_width = static_cast<float>(max_x - min_x + 1);
    const float measured_height = static_cast<float>(max_y - min_y + 1);
    // These are dense-body bounds (the outer two percent of depth pixels were
    // removed on each axis), so a small guard is sufficient. The previous
    // 1.30 padding followed by an 0.86 presentation fill compounded into only
    // 66% usable height; a sparse horizontal weapon then made the body smaller
    // still. Keep a real margin without double-shrinking the actor.
    constexpr float kSilhouettePadding = 1.12f;
    const float invariant_width = measured_width * kSilhouettePadding;
    const float invariant_height = measured_height * kSilhouettePadding;
    float crop_width_f = std::max(invariant_width,
                                  invariant_height * output_aspect);
    float crop_height_f = crop_width_f / output_aspect;
    if (crop_height_f > source_height) {
      crop_height_f = static_cast<float>(source_height);
      crop_width_f = crop_height_f * output_aspect;
    }
    crop_width_f = std::min(crop_width_f, static_cast<float>(source_width));
    crop_height_f = std::min(crop_height_f,
                             static_cast<float>(source_height));
    const uint32_t stable_width = std::clamp<uint32_t>(
        static_cast<uint32_t>(std::ceil(crop_width_f)), 1, source_width);
    const uint32_t stable_height = std::clamp<uint32_t>(
        static_cast<uint32_t>(std::ceil(crop_height_f)), 1, source_height);
    const uint32_t center_x = min_x + (max_x - min_x) / 2;
    const uint32_t center_y = min_y + (max_y - min_y) / 2;
    g_character_render.stable_crop_min_x =
        std::min(source_width - stable_width,
                 center_x > stable_width / 2
                     ? center_x - stable_width / 2 : 0u);
    g_character_render.stable_crop_min_y =
        std::min(source_height - stable_height,
                 center_y > stable_height / 2
                     ? center_y - stable_height / 2 : 0u);
    g_character_render.stable_crop_max_x =
        g_character_render.stable_crop_min_x + stable_width - 1;
    g_character_render.stable_crop_max_y =
        g_character_render.stable_crop_min_y + stable_height - 1;
    g_character_render.stable_crop_valid = true;
    PluginLog(false,
              "[CHAR_FRAME] locked stable actor crop=%u,%u..%u,%u "
              "anchor=%u,%u expected=%.1fx%.1f measured=%.1fx%.1f",
              g_character_render.stable_crop_min_x,
              g_character_render.stable_crop_min_y,
              g_character_render.stable_crop_max_x,
              g_character_render.stable_crop_max_y,
              center_x, center_y,
              g_character_render.camera_expected_width,
              g_character_render.camera_expected_height,
              measured_width, measured_height);
  }
  min_x = g_character_render.stable_crop_min_x;
  min_y = g_character_render.stable_crop_min_y;
  max_x = g_character_render.stable_crop_max_x;
  max_y = g_character_render.stable_crop_max_y;
  const uint32_t crop_width = max_x - min_x + 1;
  const uint32_t crop_height = max_y - min_y + 1;
  const float requested_zoom = std::clamp(
      g_character_render.output_zoom, 0.72f, 1.12f);
  const float occupied_fraction = std::min(0.98f, 0.94f * requested_zoom);
  const float scale = std::min(
      (static_cast<float>(output_width) * occupied_fraction) /
          static_cast<float>(crop_width),
      (static_cast<float>(output_height) * occupied_fraction) /
          static_cast<float>(crop_height));
  if (!std::isfinite(scale) || scale <= 0.0f) return false;
  const uint32_t fitted_width = std::clamp<uint32_t>(
      static_cast<uint32_t>(std::lround(crop_width * scale)), 1,
      output_width);
  const uint32_t fitted_height = std::clamp<uint32_t>(
      static_cast<uint32_t>(std::lround(crop_height * scale)), 1,
      output_height);
  const uint32_t destination_x = (output_width - fitted_width) / 2;
  const uint32_t destination_y = (output_height - fitted_height) / 2;
  std::memset(g_character_frame_rgba, 0,
              static_cast<size_t>(output_width) * output_height * 4);

  uint32_t output_covered = 0;
  for (uint32_t y = 0; y < fitted_height; ++y) {
    const float source_y = std::clamp(
        static_cast<float>(min_y) +
            (static_cast<float>(y) + 0.5f) / scale - 0.5f,
        static_cast<float>(min_y), static_cast<float>(max_y));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(source_y));
    const uint32_t y1 = std::min(max_y, y0 + 1);
    const float fy = source_y - static_cast<float>(y0);
    for (uint32_t x = 0; x < fitted_width; ++x) {
      const float source_x = std::clamp(
          static_cast<float>(min_x) +
              (static_cast<float>(x) + 0.5f) / scale - 0.5f,
          static_cast<float>(min_x), static_cast<float>(max_x));
      const uint32_t x0 = static_cast<uint32_t>(std::floor(source_x));
      const uint32_t x1 = std::min(max_x, x0 + 1);
      const float fx = source_x - static_cast<float>(x0);
      const float weights[4] = {
          (1.0f - fx) * (1.0f - fy), fx * (1.0f - fy),
          (1.0f - fx) * fy, fx * fy};
      const uint8_t* samples[4] = {
          g_character_capture_rgba +
              (static_cast<size_t>(y0) * source_width + x0) * 4,
          g_character_capture_rgba +
              (static_cast<size_t>(y0) * source_width + x1) * 4,
          g_character_capture_rgba +
              (static_cast<size_t>(y1) * source_width + x0) * 4,
          g_character_capture_rgba +
              (static_cast<size_t>(y1) * source_width + x1) * 4};
      float alpha = 0.0f;
      float premultiplied[3] = {};
      for (unsigned sample = 0; sample < 4; ++sample) {
        const float weighted_alpha =
            weights[sample] * static_cast<float>(samples[sample][3]);
        alpha += weighted_alpha;
        for (unsigned channel = 0; channel < 3; ++channel)
          premultiplied[channel] +=
              weighted_alpha * static_cast<float>(samples[sample][channel]);
      }
      uint8_t* destination = g_character_frame_rgba +
          (static_cast<size_t>(destination_y + y) * output_width +
           destination_x + x) * 4;
      destination[3] = static_cast<uint8_t>(
          std::clamp(std::lround(alpha), 0l, 255l));
      if (alpha > 0.001f) {
        ++output_covered;
        for (unsigned channel = 0; channel < 3; ++channel) {
          destination[channel] = static_cast<uint8_t>(std::clamp(
              std::lround(premultiplied[channel] / alpha), 0l, 255l));
        }
      }
    }
  }
  // A successful raw depth capture must remain visible after reframing. This
  // invariant catches an invalid crop at the producer rather than allowing a
  // transparent frame to be marked ready and cached by the bottom-screen UI.
  if (output_covered < 64) {
    PluginLog(false,
              "[CHAR_FRAME] rejected transparent reframe raw_depth=%u "
              "output_covered=%u crop=%u,%u..%u,%u",
              g_character_render.foreground_pixels, output_covered,
              min_x, min_y, max_x, max_y);
    return false;
  }
  PluginLog(false,
            "[CHAR_FRAME] depth silhouette reframed capture=%ux%u "
            "bounds=%u,%u..%u,%u output=%ux%u fitted=%ux%u at=%u,%u "
            "zoom=%.2f covered=%u",
            source_width, source_height, min_x, min_y, max_x, max_y,
            output_width, output_height, fitted_width, fitted_height,
            destination_x, destination_y,
            g_character_render.output_zoom, output_covered);
  return true;
}

bool ConvertCharacterFrame(const D3D11_MAPPED_SUBRESOURCE& mapped,
                           const D3D11_MAPPED_SUBRESOURCE& mapped_depth) {
  if (!mapped.pData || mapped.RowPitch < g_character_render.width * 4 ||
      !mapped_depth.pData ||
      mapped_depth.RowPitch < g_character_render.width * sizeof(float))
    return false;
  const bool bgra = g_character_render.format == DXGI_FORMAT_B8G8R8A8_UNORM ||
                    g_character_render.format ==
                        DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
  auto has_written_depth = [&](uint32_t x, uint32_t y) {
    float depth = 1.0f;
    const auto* source = static_cast<const uint8_t*>(mapped_depth.pData) +
        static_cast<size_t>(y) * mapped_depth.RowPitch + x * sizeof(float);
    std::memcpy(&depth, source, sizeof(depth));
    return std::isfinite(depth) && depth >= 0.0f && depth < 1.0f;
  };
  uint32_t foreground = 0;
  uint32_t edge_left = 0;
  uint32_t edge_right = 0;
  uint32_t edge_top = 0;
  uint32_t edge_bottom = 0;
  uint32_t min_x = g_character_render.width;
  uint32_t min_y = g_character_render.height;
  uint32_t max_x = 0;
  uint32_t max_y = 0;
  uint8_t minimum_rgb = 255;
  uint8_t maximum_rgb = 0;
  std::fill_n(g_character_depth_columns, g_character_render.width, 0u);
  std::fill_n(g_character_depth_rows, g_character_render.height, 0u);
  for (uint32_t y = 0; y < g_character_render.height; ++y) {
    const uint8_t* source = reinterpret_cast<const uint8_t*>(mapped.pData) +
                            static_cast<size_t>(y) * mapped.RowPitch;
    uint8_t* destination = g_character_capture_rgba +
                           static_cast<size_t>(y) *
                               g_character_render.width * 4;
    for (uint32_t x = 0; x < g_character_render.width; ++x) {
      const uint8_t red = source[x * 4 + (bgra ? 2 : 0)];
      const uint8_t green = source[x * 4 + 1];
      const uint8_t blue = source[x * 4 + (bgra ? 0 : 2)];
      // Clear color is approximately RGB(5,6,6). A real shaded actor easily
      // exceeds this threshold, while an empty render target does not. The
      // copied D32 silhouette is authoritative when available and preserves
      // legitimately black armour, hair, and shadowed skin.
      const bool covered = has_written_depth(x, y);
      destination[x * 4 + 0] = red;
      destination[x * 4 + 1] = green;
      destination[x * 4 + 2] = blue;
      // The bridge/canvas already performs alpha compositing. Keeping the
      // private clear pixels transparent removes the black portrait card
      // without touching any Skyrim material pixel.
      destination[x * 4 + 3] = covered ? 255 : 0;
      if (covered) {
        ++foreground;
        ++g_character_depth_columns[x];
        ++g_character_depth_rows[y];
        if (x == 0) ++edge_left;
        if (x + 1 == g_character_render.width) ++edge_right;
        if (y == 0) ++edge_top;
        if (y + 1 == g_character_render.height) ++edge_bottom;
        minimum_rgb = std::min({minimum_rgb, red, green, blue});
        maximum_rgb = std::max({maximum_rgb, red, green, blue});
        min_x = std::min(min_x, x);
        min_y = std::min(min_y, y);
        max_x = std::max(max_x, x);
        max_y = std::max(max_y, y);
      }
    }
  }
  g_character_render.foreground_pixels = foreground;
  g_character_render.content_min_x = foreground ? min_x : 0;
  g_character_render.content_min_y = foreground ? min_y : 0;
  g_character_render.content_max_x = foreground ? max_x : 0;
  g_character_render.content_max_y = foreground ? max_y : 0;
  g_character_render.minimum_rgb = minimum_rgb;
  g_character_render.maximum_rgb = maximum_rgb;
  const uint32_t depth_samples = foreground;
  if (foreground < 64) {
    ++g_character_render.camera_fit_retries;
    g_character_render.camera_centered_confirmations = 0;
    if (g_character_render.camera_fit_retries <= 8) {
      PluginLog(false,
                "[CHAR_FRAME] empty authoritative depth=%u; retaining "
                "confirmed working camera margin=%.2f retry=%u",
                depth_samples, g_character_render.camera_fit_margin,
                g_character_render.camera_fit_retries);
    }
    return false;
  }
  const uint64_t target_pixels =
      static_cast<uint64_t>(g_character_render.width) *
      static_cast<uint64_t>(g_character_render.height);
  if (static_cast<uint64_t>(depth_samples) * 100u >= target_pixels * 92u) {
    PluginLog(false,
              "[CHAR_FRAME] rejecting full-target depth=%u/%llu margin=%.2f",
              depth_samples, static_cast<unsigned long long>(target_pixels),
              g_character_render.camera_fit_margin);
    const float old_margin = g_character_render.camera_fit_margin;
    g_character_render.camera_fit_margin =
        std::min(2.75f, std::max(old_margin + 0.18f, old_margin * 1.22f));
    ++g_character_render.camera_fit_retries;
    g_character_render.camera_centered_confirmations = 0;
    return false;
  }
  const uint32_t edge_mask = (edge_left ? 1u : 0u) |
                             (edge_right ? 2u : 0u) |
                             (edge_top ? 4u : 0u) |
                             (edge_bottom ? 8u : 0u);
  PluginLog(false,
            "[CHAR_FRAME] raw depth silhouette capture=%ux%u "
            "bounds=%u,%u..%u,%u edges=%u/%u/%u/%u mask=%02X "
            "complete=%u overscan=%u",
            g_character_render.width, g_character_render.height,
            min_x, min_y, max_x, max_y, edge_left, edge_right,
            edge_top, edge_bottom, edge_mask, edge_mask ? 0u : 1u,
            kCharacterCaptureScale);
  // A drawn weapon can be hundreds of pixels from the body while contributing
  // only a thin line of depth. Fitting the full extrema made that sparse line
  // dictate the portrait width and shrink the actual actor to a small patch.
  // Trim equal pixel mass—not a fixed number of columns—from both ends. This
  // remains stable for armour and body shapes while ignoring only the outer
  // two percent of the submitted Skyrim silhouette on each axis.
  auto trimmed_extent = [](const uint32_t* histogram, uint32_t count,
                           uint32_t total, uint32_t full_min,
                           uint32_t full_max, uint32_t* trimmed_min,
                           uint32_t* trimmed_max) {
    if (!histogram || !count || !trimmed_min || !trimmed_max ||
        full_min > full_max || full_max >= count) {
      return false;
    }
    const uint32_t trim = total / 50u;
    uint64_t accumulated = 0;
    uint32_t low = full_min;
    for (; low < full_max; ++low) {
      accumulated += histogram[low];
      if (accumulated > trim) break;
    }
    accumulated = 0;
    uint32_t high = full_max;
    for (; high > low; --high) {
      accumulated += histogram[high];
      if (accumulated > trim) break;
    }
    if (low >= high) return false;
    *trimmed_min = low;
    *trimmed_max = high;
    return true;
  };
  uint32_t portrait_min_x = min_x;
  uint32_t portrait_min_y = min_y;
  uint32_t portrait_max_x = max_x;
  uint32_t portrait_max_y = max_y;
  trimmed_extent(g_character_depth_columns, g_character_render.width,
                 foreground, min_x, max_x, &portrait_min_x,
                 &portrait_max_x);
  trimmed_extent(g_character_depth_rows, g_character_render.height,
                 foreground, min_y, max_y, &portrait_min_y,
                 &portrait_max_y);
  PluginLog(false,
            "[CHAR_FRAME] dense portrait bounds=%u,%u..%u,%u "
            "from full=%u,%u..%u,%u trim_pixels=%u",
            portrait_min_x, portrait_min_y, portrait_max_x,
            portrait_max_y, min_x, min_y, max_x, max_y, foreground / 50u);
  if (!ReframeCharacterCapture(portrait_min_x, portrait_min_y,
                               portrait_max_x, portrait_max_y))
    return false;
  // A depth-covered frame is an authoritative Skyrim actor draw. The
  // confirmed-working build published this first frame immediately. Rejecting
  // it because its silhouette touched an edge was the regression: every retry
  // changed the shared UI3D projection, and the actor eventually left the
  // target before any frame reached the proxy. Preserve the exact working
  // camera and publish the first real draw. Transparency still comes solely
  // from the copied Skyrim depth buffer above.
  PluginLog(false,
            "[CHAR_FRAME] accepted authoritative actor "
            "bounds=%u,%u..%u,%u depth=%u fixed_camera=1",
            min_x, min_y, max_x, max_y, depth_samples);
  g_character_render.camera_fit_retries = 0;
  g_character_render.camera_centered_confirmations = 0;
  return true;
}

bool ConfigureCharacterCamera(
    void* manager, void* character_root,
    const dis::CharacterPreviewSurface& control) {
  if (!manager || !character_root || !RegionAllows(manager, 0x118) ||
      !RegionAllows(character_root, 0xF4))
    return false;
  void* camera = ReadOr<void*>(reinterpret_cast<uint8_t*>(manager) + 0x20,
                               nullptr);
  if (!camera || !RegionAllows(camera, sizeof(void*))) return false;
  float root_bound[4] = {};
  if (!Read(reinterpret_cast<uint8_t*>(character_root) + 0xE4,
            &root_bound) ||
      !std::isfinite(root_bound[0]) ||
      !std::isfinite(root_bound[1]) ||
      !std::isfinite(root_bound[2]) ||
      !std::isfinite(root_bound[3]) || root_bound[3] <= 0.01f ||
      root_bound[3] > 100000.0f)
    return false;
  using SetPositionFn = void(*)(void*, const float*);
  using LookAtFn = void(*)(void*, const float*);
  auto set_position =
      reinterpret_cast<SetPositionFn>(g_base + kUI3DSetCameraPosition);
  auto look_at = reinterpret_cast<LookAtFn>(g_base + kUI3DLookAt);
  if (!RegionAllows(reinterpret_cast<void*>(set_position), 1, true) ||
      !RegionAllows(reinterpret_cast<void*>(look_at), 1, true))
    return false;

  // Keep the manager camera and its 45-degree projection at the exact values
  // used by the confirmed-working build. The actor remains at its original
  // evaluated world transform so its flattened skin palette stays coherent;
  // only this private UI camera moves.
  constexpr float fov_parameter = 45.0f;
  float target[3] = {root_bound[0], root_bound[1], root_bound[2]};
  float tangent_horizontal = 0.0f;
  float tangent_vertical = 0.0f;
  if (!SetPrivateUi3DCameraFov(
          manager, camera, fov_parameter,
          g_character_render.width, g_character_render.height,
          1.0f, g_character_render.primary_aspect,
          &tangent_horizontal, &tangent_vertical)) {
    return false;
  }
  // Fit the native-scale actor with camera distance, never root scaling. The
  // first term keeps the near side of the actor sphere in front of the fitted
  // cone; the second fits its limiting horizontal/vertical projected radius.
  // With the prior 0.535 root scale this evaluates to the proven ~600-unit
  // camera distance; at scale 1 it moves the camera back proportionally while
  // preserving every Skyrim skin and attachment transform.
  const float limiting_tangent =
      std::min(tangent_horizontal, tangent_vertical);
  const float fitted_distance = std::clamp(
      root_bound[3] + root_bound[3] *
          g_character_render.camera_fit_margin / limiting_tangent,
      60.0f, 250000.0f);
  const float zoom = std::clamp(control.zoom, 0.65f, 1.75f);
  const float distance = fitted_distance / zoom;
  SceneTransform actor_world = {};
  if (!ReadSceneTransform(character_root, &actor_world)) return false;
  const float angle = control.rotation_degrees *
                      (3.14159265358979323846f / 180.0f);
  const float cosine = std::cos(angle);
  const float sine = std::sin(angle);
  // Local +Y is the actor's forward axis and local +X is its right axis. Orbit
  // around those copied world axes so a zero-degree preview remains aligned to
  // the actor regardless of the player's current world heading.
  const float direction[3] = {
      actor_world.rotate[1] * cosine + actor_world.rotate[0] * sine,
      actor_world.rotate[4] * cosine + actor_world.rotate[3] * sine,
      actor_world.rotate[7] * cosine + actor_world.rotate[6] * sine};
  float position[3] = {target[0] + direction[0] * distance,
                       target[1] + direction[1] * distance,
                       target[2] + direction[2] * distance};
  set_position(manager, position);
  look_at(manager, target);
  g_character_render.camera_last_distance = distance;
  g_character_render.camera_tangent_horizontal = tangent_horizontal;
  g_character_render.camera_tangent_vertical = tangent_vertical;
  g_character_render.camera_expected_width =
      g_character_render.width *
      (2.0f * root_bound[3]) /
      std::max(0.001f, 2.0f * distance * tangent_horizontal);
  g_character_render.camera_expected_height =
      g_character_render.height *
      (2.0f * root_bound[3]) /
      std::max(0.001f, 2.0f * distance * tangent_vertical);
  if (g_character_render.render_attempts == 1 ||
      g_character_render.camera_fit_retries) {
    PluginLog(false,
              "[CHAR_FRAME] working native camera "
              "bound=(%.1f,%.1f,%.1f r=%.1f) "
              "target=(%.1f,%.1f,%.1f) distance=%.1f margin=%.2f fov=%.1f "
              "zoom=%.2f slopes=(%.6f,%.6f) pan=(%.2f,%.2f)",
              root_bound[0], root_bound[1], root_bound[2], root_bound[3],
              target[0], target[1], target[2], distance,
              g_character_render.camera_fit_margin, fov_parameter,
              zoom,
              tangent_horizontal, tangent_vertical,
              g_character_render.camera_pan_x,
              g_character_render.camera_pan_z);
  }
  return true;
}

// UI3D render calls replace more than the output-merger and rasterizer state.
// Keep this bounded and allocation-free: Skyrim uses no dynamic shader class
// instances here, and the first 16 SRV slots cover the UI/material bindings.
struct CharacterPipelineState {
  ID3D11InputLayout* input_layout = nullptr;
  ID3D11Buffer* vertex_buffers[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
  UINT vertex_strides[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
  UINT vertex_offsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
  ID3D11Buffer* index_buffer = nullptr;
  DXGI_FORMAT index_format = DXGI_FORMAT_UNKNOWN;
  UINT index_offset = 0;
  D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;

  ID3D11VertexShader* vertex_shader = nullptr;
  ID3D11PixelShader* pixel_shader = nullptr;
  ID3D11GeometryShader* geometry_shader = nullptr;
  ID3D11Buffer* vertex_constants[
      D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
  ID3D11Buffer* pixel_constants[
      D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
  ID3D11Buffer* geometry_constants[
      D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
  static constexpr UINT kResourceSlots = 16;
  ID3D11ShaderResourceView* vertex_resources[kResourceSlots] = {};
  ID3D11ShaderResourceView* pixel_resources[kResourceSlots] = {};
  ID3D11ShaderResourceView* geometry_resources[kResourceSlots] = {};
  ID3D11SamplerState* vertex_samplers[
      D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
  ID3D11SamplerState* pixel_samplers[
      D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
  ID3D11SamplerState* geometry_samplers[
      D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};

  void Capture(ID3D11DeviceContext* context) {
    context->IAGetInputLayout(&input_layout);
    context->IAGetVertexBuffers(
        0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, vertex_buffers,
        vertex_strides, vertex_offsets);
    context->IAGetIndexBuffer(&index_buffer, &index_format, &index_offset);
    context->IAGetPrimitiveTopology(&topology);
    context->VSGetShader(&vertex_shader, nullptr, nullptr);
    context->PSGetShader(&pixel_shader, nullptr, nullptr);
    context->GSGetShader(&geometry_shader, nullptr, nullptr);
    context->VSGetConstantBuffers(
        0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,
        vertex_constants);
    context->PSGetConstantBuffers(
        0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,
        pixel_constants);
    context->GSGetConstantBuffers(
        0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,
        geometry_constants);
    context->VSGetShaderResources(0, kResourceSlots, vertex_resources);
    context->PSGetShaderResources(0, kResourceSlots, pixel_resources);
    context->GSGetShaderResources(0, kResourceSlots, geometry_resources);
    context->VSGetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT,
                           vertex_samplers);
    context->PSGetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT,
                           pixel_samplers);
    context->GSGetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT,
                           geometry_samplers);
  }

  void Restore(ID3D11DeviceContext* context) {
    context->IASetInputLayout(input_layout);
    context->IASetVertexBuffers(
        0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, vertex_buffers,
        vertex_strides, vertex_offsets);
    context->IASetIndexBuffer(index_buffer, index_format, index_offset);
    context->IASetPrimitiveTopology(topology);
    context->VSSetShader(vertex_shader, nullptr, 0);
    context->PSSetShader(pixel_shader, nullptr, 0);
    context->GSSetShader(geometry_shader, nullptr, 0);
    context->VSSetConstantBuffers(
        0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,
        vertex_constants);
    context->PSSetConstantBuffers(
        0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,
        pixel_constants);
    context->GSSetConstantBuffers(
        0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,
        geometry_constants);
    context->VSSetShaderResources(0, kResourceSlots, vertex_resources);
    context->PSSetShaderResources(0, kResourceSlots, pixel_resources);
    context->GSSetShaderResources(0, kResourceSlots, geometry_resources);
    context->VSSetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT,
                           vertex_samplers);
    context->PSSetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT,
                           pixel_samplers);
    context->GSSetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT,
                           geometry_samplers);
  }

  ~CharacterPipelineState() {
    if (input_layout) input_layout->Release();
    for (auto* resource : vertex_buffers)
      if (resource) resource->Release();
    if (index_buffer) index_buffer->Release();
    if (vertex_shader) vertex_shader->Release();
    if (pixel_shader) pixel_shader->Release();
    if (geometry_shader) geometry_shader->Release();
    for (auto* resource : vertex_constants)
      if (resource) resource->Release();
    for (auto* resource : pixel_constants)
      if (resource) resource->Release();
    for (auto* resource : geometry_constants)
      if (resource) resource->Release();
    for (auto* resource : vertex_resources)
      if (resource) resource->Release();
    for (auto* resource : pixel_resources)
      if (resource) resource->Release();
    for (auto* resource : geometry_resources)
      if (resource) resource->Release();
    for (auto* resource : vertex_samplers)
      if (resource) resource->Release();
    for (auto* resource : pixel_samplers)
      if (resource) resource->Release();
    for (auto* resource : geometry_samplers)
      if (resource) resource->Release();
  }
};

bool RenderCharacterOffscreen(IDXGISwapChain* chain, void* manager,
                              const dis::CharacterPreviewSurface& control) {
  g_character_render.engine_render_result = 0;
  if (g_character_render.readback_ready) {
    // TickCharacterPreview consumes completed pixels independently of scene
    // ownership. A submitted clone is retired at Present and therefore cannot
    // be required to survive until this event query completes.
    g_character_render.engine_render_result = 1;
    return false;
  }
  if (g_character_render.readback_pending) {
    g_character_render.engine_render_result = 2;
    return false;
  }
  if (g_deferred_ui3d_target.active) {
    g_character_render.engine_render_result = 2;
    return false;
  }
  ++g_character_render.render_attempts;
  const uint32_t output_width =
      static_cast<uint32_t>(std::max(1, control.width));
  const uint32_t output_height =
      static_cast<uint32_t>(std::max(1, control.height));
  if (output_width > dis::kCharacterPreviewMaxWidth ||
      output_height > dis::kCharacterPreviewMaxHeight)
    return false;
  const uint32_t capture_width = output_width * kCharacterCaptureScale;
  const uint32_t capture_height = output_height * kCharacterCaptureScale;
  if (!chain || !manager || !g_character_render.active_clone ||
      control.width <= 0 || control.height <= 0 ||
      !EnsureCharacterRenderTextures(chain, capture_width, capture_height))
    return false;
  g_character_render.output_width = output_width;
  g_character_render.output_height = output_height;
  g_character_render.output_zoom = control.zoom;
  if (!UpdateCharacterTransform(control)) return false;

  ID3D11Device* device = nullptr;
  if (FAILED(chain->GetDevice(__uuidof(ID3D11Device),
                              reinterpret_cast<void**>(&device))) ||
      !device)
    return false;
  ID3D11DeviceContext* context = nullptr;
  device->GetImmediateContext(&context);
  device->Release();
  if (!context) return false;

  auto* renderer = reinterpret_cast<uint8_t*>(g_base + kRendererSingleton);
  auto* render_target_manager =
      reinterpret_cast<uint8_t*>(g_base + kRenderTargetManager);
  auto* framebuffer = reinterpret_cast<RenderTargetDataRaw*>(
      renderer + kRendererFramebufferOffset);
  auto* framebuffer_width = reinterpret_cast<uint32_t*>(
      render_target_manager + kRendererFramebufferWidthOffset);
  auto* framebuffer_height = reinterpret_cast<uint32_t*>(
      render_target_manager + kRendererFramebufferHeightOffset);
  auto* depth_stencil_width = reinterpret_cast<uint32_t*>(
      render_target_manager + kRendererDepthStencilWidthOffset);
  auto* depth_stencil_height = reinterpret_cast<uint32_t*>(
      render_target_manager + kRendererDepthStencilHeightOffset);
  if (!RegionWritable(framebuffer, sizeof(*framebuffer)) ||
      !RegionWritable(framebuffer_width, sizeof(*framebuffer_width)) ||
      !RegionWritable(framebuffer_height, sizeof(*framebuffer_height)) ||
      !RegionWritable(depth_stencil_width, sizeof(*depth_stencil_width)) ||
      !RegionWritable(depth_stencil_height, sizeof(*depth_stencil_height))) {
    context->Release();
    return false;
  }

  ID3D11RenderTargetView* old_views[
      D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
  ID3D11DepthStencilView* old_depth = nullptr;
  context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
                              old_views, &old_depth);
  ID3D11BlendState* old_blend = nullptr;
  FLOAT old_blend_factor[4] = {};
  UINT old_sample_mask = 0xFFFFFFFFu;
  context->OMGetBlendState(&old_blend, old_blend_factor, &old_sample_mask);
  ID3D11DepthStencilState* old_depth_state = nullptr;
  UINT old_stencil_reference = 0;
  context->OMGetDepthStencilState(&old_depth_state, &old_stencil_reference);
  ID3D11RasterizerState* old_rasterizer = nullptr;
  context->RSGetState(&old_rasterizer);
  UINT viewport_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
  D3D11_VIEWPORT old_viewports[
      D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
  context->RSGetViewports(&viewport_count, old_viewports);
  UINT scissor_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
  D3D11_RECT old_scissors[
      D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
  context->RSGetScissorRects(&scissor_count, old_scissors);

  const RenderTargetDataRaw saved_framebuffer = *framebuffer;
  const uint32_t saved_framebuffer_width = *framebuffer_width;
  const uint32_t saved_framebuffer_height = *framebuffer_height;
  const uint32_t saved_depth_stencil_width = *depth_stencil_width;
  const uint32_t saved_depth_stencil_height = *depth_stencil_height;
  CharacterPipelineState pipeline_state;
  pipeline_state.Capture(context);
  bool deferred_target_armed = false;
  bool camera_saved = false;
  bool preview_light_state_applied = false;
  bool engine_restore_ok = true;
  int32_t saved_scheme = -1;
  using SetLightSchemeFn = void(*)(void*, int32_t);
  auto set_light_scheme = reinterpret_cast<SetLightSchemeFn>(
      g_base + kUI3DSetCurrentLightScheme);
  auto* current_scheme = reinterpret_cast<int32_t*>(
      reinterpret_cast<uint8_t*>(manager) + 0x90);

  auto restore_light_scheme = [&]() {
    if (preview_light_state_applied) {
      if (!RegionAllows(manager, 0x94) ||
          !RegionAllows(reinterpret_cast<void*>(set_light_scheme), 1, true) ||
          !RegionWritable(current_scheme, sizeof(*current_scheme))) {
        engine_restore_ok = false;
      } else {
        // The helper removes the lights named by currentLightScheme, but does
        // not update that field. During the preview the active lights are the
        // Inventory set even though the field is scene 5 for Render(). Restore
        // that real active identity first, then return both lights and field to
        // Skyrim's saved state.
        if (!WriteValue(current_scheme, kItemPreviewScheme)) {
          engine_restore_ok = false;
        } else if (saved_scheme != kItemPreviewScheme) {
          set_light_scheme(manager, saved_scheme);
        }
        if (!WriteValue(current_scheme, saved_scheme))
          engine_restore_ok = false;
      }
      preview_light_state_applied = false;
    }
  };

  auto restore = [&]() {
    restore_light_scheme();
    if (camera_saved) {
      if (!RestoreSharedCharacterCamera(manager)) engine_restore_ok = false;
      camera_saved = false;
    }
    if (!deferred_target_armed) {
      *framebuffer = saved_framebuffer;
      *framebuffer_width = saved_framebuffer_width;
      *framebuffer_height = saved_framebuffer_height;
      *depth_stencil_width = saved_depth_stencil_width;
      *depth_stencil_height = saved_depth_stencil_height;
      context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
                                  old_views, old_depth);
    }
    context->OMSetBlendState(old_blend, old_blend_factor, old_sample_mask);
    context->OMSetDepthStencilState(old_depth_state, old_stencil_reference);
    context->RSSetState(old_rasterizer);
    if (!deferred_target_armed) {
      context->RSSetViewports(viewport_count,
                              viewport_count ? old_viewports : nullptr);
      context->RSSetScissorRects(scissor_count,
                                 scissor_count ? old_scissors : nullptr);
    }
    pipeline_state.Restore(context);
  };

  RenderTargetDataRaw preview_target = {};
  preview_target.texture = g_character_render.render_texture;
  preview_target.texture_copy = g_character_render.render_texture;
  preview_target.render_view = g_character_render.render_view;
  preview_target.shader_view = g_character_render.shader_view;
  preview_target.shader_copy = g_character_render.shader_view;
  *framebuffer = preview_target;
  *framebuffer_width = g_character_render.width;
  *framebuffer_height = g_character_render.height;
  *depth_stencil_width = g_character_render.width;
  *depth_stencil_height = g_character_render.height;

  context->OMSetRenderTargets(1, &g_character_render.render_view,
                              g_character_render.depth_view);
  const D3D11_VIEWPORT viewport = {
      0.0f, 0.0f, static_cast<float>(g_character_render.width),
      static_cast<float>(g_character_render.height), 0.0f, 1.0f};
  context->RSSetViewports(1, &viewport);
  const D3D11_RECT scissor = {
      0, 0, static_cast<LONG>(g_character_render.width),
      static_cast<LONG>(g_character_render.height)};
  context->RSSetScissorRects(1, &scissor);
  const float background[4] = {0.018f, 0.024f, 0.024f, 1.0f};
  context->ClearRenderTargetView(g_character_render.render_view, background);
  context->ClearDepthStencilView(g_character_render.depth_view,
                                 D3D11_CLEAR_DEPTH, 1.0f, 0);

  // The actor's deferred skin draw is associated with Skyrim's manager-owned
  // UI3D camera. Save it before applying the portrait transform and restore it
  // immediately after submission; inventory items continue to use the fully
  // isolated private-camera path.
  if (!SaveSharedCharacterCamera(manager)) {
    restore();
    if (old_rasterizer) old_rasterizer->Release();
    if (old_depth_state) old_depth_state->Release();
    if (old_blend) old_blend->Release();
    if (old_depth) old_depth->Release();
    for (auto* view : old_views)
      if (view) view->Release();
    context->Release();
    return false;
  }
  camera_saved = true;
  if (!ConfigureCharacterCamera(manager,
                                g_character_render.active_clone,
                                control)) {
    restore();
    if (old_rasterizer) old_rasterizer->Release();
    if (old_depth_state) old_depth_state->Release();
    if (old_blend) old_blend->Release();
    if (old_depth) old_depth->Release();
    for (auto* view : old_views)
      if (view) view->Release();
    context->Release();
    return false;
  }
  if (g_character_render.render_attempts == 1)
    LogUi3DSceneState("[CHAR_PREVIEW]", manager,
                      g_character_render.active_clone);

  // UI3DSceneManager::Render refuses a non-zero scene when it differs from
  // currentLightScheme. The actor must remain in isolated scene 5, but that
  // unknown scheme has no useful menu lights and produced the verified dark
  // portrait. Activate Skyrim's own Inventory light set (scheme 1), then put
  // only the dispatch selector on scene 5. This reuses the exact native menu
  // lights/material shaders without moving the actor into Inventory3D's async
  // scene or sharing its model lifetime.
  saved_scheme = ReadOr<int32_t>(current_scheme, -1);
  if (!RegionWritable(current_scheme, sizeof(*current_scheme)) ||
      saved_scheme < 0 || saved_scheme >= 8 ||
      (saved_scheme != kItemPreviewScheme &&
       !RegionAllows(reinterpret_cast<void*>(set_light_scheme), 1, true))) {
    restore();
    if (old_rasterizer) old_rasterizer->Release();
    if (old_depth_state) old_depth_state->Release();
    if (old_blend) old_blend->Release();
    if (old_depth) old_depth->Release();
    for (auto* view : old_views)
      if (view) view->Release();
    context->Release();
    return false;
  }
  if (saved_scheme != kItemPreviewScheme)
    set_light_scheme(manager, kItemPreviewScheme);
  preview_light_state_applied = true;
  if (!WriteValue(current_scheme, kCharacterPreviewScheme)) {
    engine_restore_ok = false;
    restore();
    if (old_rasterizer) old_rasterizer->Release();
    if (old_depth_state) old_depth_state->Release();
    if (old_blend) old_blend->Release();
    if (old_depth) old_depth->Release();
    for (auto* view : old_views)
      if (view) view->Release();
    context->Release();
    g_native_render_reset_revision.fetch_add(1,
                                              std::memory_order_acq_rel);
    return false;
  }

  void* camera = ReadOr<void*>(
      reinterpret_cast<uint8_t*>(manager) + 0x20, nullptr);
  using RenderFn = void(*)(void*, int32_t, void*, bool);
  auto render = reinterpret_cast<RenderFn>(g_base + kUI3DRender);
  if (!camera || !RegionAllows(camera, sizeof(void*)) ||
      !RegionAllows(reinterpret_cast<void*>(render), 1, true)) {
    restore();
    if (old_rasterizer) old_rasterizer->Release();
    if (old_depth_state) old_depth_state->Release();
    if (old_blend) old_blend->Release();
    if (old_depth) old_depth->Release();
    for (auto* view : old_views)
      if (view) view->Release();
    context->Release();
    if (!engine_restore_ok)
      g_native_render_reset_revision.fetch_add(1,
                                                std::memory_order_acq_rel);
    return false;
  }
  const UiRendererCounters counters_before = ReadUiRendererCounters();
  LONG permission_value = 0;
  const bool diagnostic_render =
      g_character_render.render_attempts == 1 ||
      GetTickCount64() - g_character_render.last_diagnostic_log_ms >= 10000;
  GpuPipelineProbe pipeline_probe;
  {
    ScopedUi3DRenderPermission ui3d_permission;
    if (!ui3d_permission) {
      PluginLog(false,
                "[CHAR_PREVIEW] UI3D permission unavailable ui_valid=%u "
                "disable_pause=%u custom=%u gate_failures=%llu clone=%p",
                counters_before.valid ? 1u : 0u,
                counters_before.disable_pause,
                counters_before.custom_rendering,
                static_cast<unsigned long long>(
                    g_ui3d_gate_failures.load(std::memory_order_relaxed)),
                g_character_render.active_clone);
      restore();
      if (old_rasterizer) old_rasterizer->Release();
      if (old_depth_state) old_depth_state->Release();
      if (old_blend) old_blend->Release();
      if (old_depth) old_depth->Release();
      for (auto* view : old_views)
        if (view) view->Release();
      context->Release();
      return false;
    }
    permission_value = ui3d_permission.observed_value();
    ID3D11Device* trace_device = nullptr;
    if (diagnostic_render) {
      context->GetDevice(&trace_device);
      pipeline_probe.Start(trace_device, context);
      if (trace_device) trace_device->Release();
    }
    const bool culling_hook_ready = InstallUi3DTraceHooks(manager);
    const bool trace_render = diagnostic_render && culling_hook_ready;
    if (!culling_hook_ready) {
      PluginLog(false,
                "[CHAR_PREVIEW] UI3D diagnostics unavailable clone=%p",
                g_character_render.active_clone);
    }
    if (trace_render) {
      BeginUi3DFrameTrace(DeferredUi3DTargetOwner::kCharacter, manager,
                          g_character_render.active_clone,
                          g_character_render.render_view);
      LogUi3DObjectState("[UI3D_SUBJECT_BEFORE_DISPATCH]",
                         g_character_render.active_clone, 0);
      LogUi3DControllerChain(
          "[UI3D_SUBJECT_CONTROLLER_BEFORE_DISPATCH]",
          g_character_render.active_clone);
    }
    {
      ScopedPrivateUi3DRender private_render(
          DeferredUi3DTargetOwner::kCharacter, g_character_render.width,
          g_character_render.height, g_character_render.render_view,
          g_character_render.depth_view);
      // A cloned actor uses Skyrim's native depth-prepass accumulator followed
      // by its color accumulator. The color pass intentionally disables depth
      // writes and tests against the prepass; omitting the first pass leaves an
      // empty depth texture and makes weapons/scabbards render through the
      // body. The accumulator hook above keeps the private RTV/depth bound when
      // Skyrim restores the gameplay OM target between these two native passes.
      render(manager, kCharacterPreviewScheme, camera, true);
      // UI3DSceneManager::Render has one more renderer-state epilogue after
      // the final accumulator hook returns.  The device trace proves that
      // epilogue republishes the 1280x720 gameplay RTV/depth even though both
      // actor passes submitted against our private target.  Re-assert the
      // exact private OM/viewport/scissor while this scoped transaction still
      // owns them, so ArmDeferredUi3DTarget can transfer a valid ownership
      // token to Present instead of rejecting the completed actor dispatch.
      ApplyPrivateUi3DViewportAtSubmission();
    }
    if (trace_render) EndUi3DFrameTrace();
    pipeline_probe.Stop(context);
    if (diagnostic_render) {
      LogGpuOutputBinding("[CHAR_PREVIEW]", context,
                          g_character_render.render_view,
                          g_character_render.render_texture,
                          *framebuffer_width, *framebuffer_height);
    }
  }
  g_character_render.engine_render_result = 2;
  if (diagnostic_render) {
    PluginLog(false,
              "[CHAR_PREVIEW] UI3D dispatched clone=%p camera=%p scheme=%d "
              "ui_valid=%u disable_pause_before=%u custom=%u "
              "permission_value=%ld scenes=%u/%u/%u",
              g_character_render.active_clone, camera,
              kCharacterPreviewScheme, counters_before.valid ? 1u : 0u,
              counters_before.disable_pause,
              counters_before.custom_rendering,
              static_cast<long>(permission_value),
              g_character_render.scene_objects,
              g_character_render.geometry_objects,
              g_character_render.bounded_objects);
    g_character_render.last_diagnostic_log_ms = GetTickCount64();
  }

  ID3D11Query* deferred_pipeline_query = nullptr;
  ID3D11Query* deferred_occlusion_query = nullptr;
  pipeline_probe.Detach(&deferred_pipeline_query,
                        &deferred_occlusion_query);
  deferred_target_armed = ArmDeferredUi3DTarget(
      DeferredUi3DTargetOwner::kCharacter, framebuffer, saved_framebuffer,
      framebuffer_width, saved_framebuffer_width, framebuffer_height,
      saved_framebuffer_height, depth_stencil_width,
      saved_depth_stencil_width, depth_stencil_height,
      saved_depth_stencil_height, old_views, old_depth, viewport_count,
      old_viewports, scissor_count, old_scissors, deferred_pipeline_query,
      deferred_occlusion_query);
  if (!deferred_target_armed) {
    if (deferred_pipeline_query) deferred_pipeline_query->Release();
    if (deferred_occlusion_query) deferred_occlusion_query->Release();
    PluginLog(false,
              "[GPU] [CHAR_PREVIEW] framebuffer lease unavailable clone=%p",
              g_character_render.active_clone);
    g_character_render.engine_render_result = 0;
  } else {
    // Transfer the active Inventory lights to the same short Present lease as
    // the deferred skinned draw. The shared camera is still restored below,
    // so this cannot reintroduce the gameplay-camera shake.
    g_deferred_ui3d_target.light_manager = manager;
    g_deferred_ui3d_target.current_light_scheme = current_scheme;
    g_deferred_ui3d_target.saved_light_scheme = saved_scheme;
    g_deferred_ui3d_target.active_light_scheme = kItemPreviewScheme;
    g_deferred_ui3d_target.dispatch_light_scheme =
        kCharacterPreviewScheme;
    g_deferred_ui3d_target.light_override_active =
        preview_light_state_applied;
    preview_light_state_applied = false;
    g_character_render.submitted_scene_pending_retirement = true;
  }

  // Return the manager camera immediately. The framebuffer record, output
  // binding, and native Inventory light set survive only until the primary
  // Present hook, where Skyrim 1.5.97 drains skinned UI3D work. Keeping the
  // camera out of that lease prevents the world camera from inheriting any
  // portrait transform.
  restore();
  g_character_render.readback_pending = deferred_target_armed;
  g_character_render.readback_copy_issued = false;
  g_character_render.readback_ready = false;
  g_character_render.readback_success = false;
  g_character_render.readback_dispatched_ms =
      deferred_target_armed ? GetTickCount64() : 0;
  g_character_render.readback_world_epoch = deferred_target_armed
      ? g_character_render.active_world_epoch
      : 0;
  g_character_render.readback_source_revision = deferred_target_armed
      ? g_character_render.active_source_revision
      : 0;
  g_character_render.readback_appearance_signature = deferred_target_armed
      ? g_character_adopted_signature.load(std::memory_order_acquire)
      : 0;
  g_character_render.readback_reset_revision = deferred_target_armed
      ? g_character_reset_revision.load(std::memory_order_acquire)
      : 0;
  g_character_inflight_signature.store(
      g_character_render.readback_appearance_signature,
      std::memory_order_release);
  g_character_inflight_world_epoch.store(
      g_character_render.readback_world_epoch, std::memory_order_release);
  if (old_rasterizer) old_rasterizer->Release();
  if (old_depth_state) old_depth_state->Release();
  if (old_blend) old_blend->Release();
  if (!deferred_target_armed) {
    if (old_depth) old_depth->Release();
    for (auto* view : old_views)
      if (view) view->Release();
  }
  context->Release();
  if (!engine_restore_ok) {
    RestoreDeferredUi3DTarget(chain, "character engine-state failure");
    g_character_render.readback_pending = false;
    g_character_render.readback_copy_issued = false;
    g_character_render.readback_dispatched_ms = 0;
    g_character_render.readback_world_epoch = 0;
    g_character_render.readback_source_revision = 0;
    g_character_render.readback_appearance_signature = 0;
    g_character_render.readback_reset_revision = 0;
    g_character_inflight_signature.store(0, std::memory_order_release);
    g_character_inflight_world_epoch.store(0, std::memory_order_release);
    g_native_render_reset_revision.fetch_add(1, std::memory_order_acq_rel);
    return false;
  }
  return false;
}

enum class CharacterReadbackResult : uint8_t {
  kNone,
  kSuccess,
  kEmpty,
  kStale,
};

CharacterReadbackResult ConsumeCompletedCharacterReadback(bool publish) {
  if (!g_character_render.readback_ready)
    return CharacterReadbackResult::kNone;
  const uint64_t world_epoch = g_character_render.readback_world_epoch;
  const uint64_t source_revision =
      g_character_render.readback_source_revision;
  const uint64_t appearance_signature =
      g_character_render.readback_appearance_signature;
  const uint64_t reset_revision =
      g_character_render.readback_reset_revision;
  const bool same_source = world_epoch != 0 && source_revision != 0 &&
      world_epoch == g_world_epoch.load(std::memory_order_acquire) &&
      reset_revision ==
          g_character_reset_revision.load(std::memory_order_acquire) &&
      LiveWorldReadyForNativeRendering();
  const bool converted = same_source && g_character_render.readback_success;
  g_character_render.readback_ready = false;
  g_character_render.readback_success = false;
  g_character_render.readback_world_epoch = 0;
  g_character_render.readback_source_revision = 0;
  g_character_render.readback_appearance_signature = 0;
  g_character_render.readback_reset_revision = 0;
  g_character_render.engine_render_result = same_source ? 1u : 0u;
  if (!same_source) {
    g_character_inflight_signature.store(0, std::memory_order_release);
    g_character_inflight_world_epoch.store(0, std::memory_order_release);
    PluginLog(false,
              "[CHAR_LIFECYCLE] discarded stale completed frame "
              "epoch=%llu revision=%llu reset=%llu/%llu",
              static_cast<unsigned long long>(world_epoch),
              static_cast<unsigned long long>(source_revision),
              static_cast<unsigned long long>(reset_revision),
              static_cast<unsigned long long>(
                  g_character_reset_revision.load(std::memory_order_acquire)));
    return CharacterReadbackResult::kStale;
  }
  if (!converted) {
    g_character_inflight_signature.store(0, std::memory_order_release);
    g_character_inflight_world_epoch.store(0, std::memory_order_release);
    ++g_character_render.render_empty_frames;
    return CharacterReadbackResult::kEmpty;
  }

  ++g_character_render.render_successes;
  g_character_render.consecutive_empty_frames = 0;
  g_character_render.empty_render_suspended = false;
  g_character_render.has_good_frame = true;
  g_character_render.cached_world_epoch = world_epoch;
  g_character_render.cached_source_revision = source_revision;
  g_character_render.cached_appearance_signature = appearance_signature;
  g_character_cached_world_epoch.store(world_epoch,
                                        std::memory_order_release);
  g_character_cached_signature.store(appearance_signature,
                                      std::memory_order_release);
  // Publish the durable identity before removing the in-flight identity so a
  // concurrent player-update sample always observes one valid owner.
  g_character_inflight_signature.store(0, std::memory_order_release);
  g_character_inflight_world_epoch.store(0, std::memory_order_release);

  if (publish && g_character_render.output_width &&
      g_character_render.output_height && g_submit_character_preview) {
    if (g_set_character_ready) g_set_character_ready(1);
    g_submit_character_preview(g_character_render.output_width,
                               g_character_render.output_height,
                               g_character_frame_rgba,
                               g_character_render.output_width * 4,
                               source_revision);
    const bool still_current =
        world_epoch == g_world_epoch.load(std::memory_order_acquire) &&
        reset_revision ==
            g_character_reset_revision.load(std::memory_order_acquire) &&
        LiveWorldReadyForNativeRendering();
    if (!still_current) {
      g_character_render.has_good_frame = false;
      g_character_render.cached_world_epoch = 0;
      g_character_render.cached_source_revision = 0;
      g_character_render.cached_appearance_signature = 0;
      g_character_cached_world_epoch.store(0, std::memory_order_release);
      g_character_cached_signature.store(0, std::memory_order_release);
      if (g_set_character_ready) g_set_character_ready(0);
      return CharacterReadbackResult::kStale;
    }
    if (g_set_character_ready) g_set_character_ready(1);
  }
  PluginLog(false,
            "[CHAR_LIFECYCLE] cached completed actor frame "
            "epoch=%llu revision=%llu signature=%016llX publish=%u",
            static_cast<unsigned long long>(world_epoch),
            static_cast<unsigned long long>(source_revision),
            static_cast<unsigned long long>(appearance_signature),
            publish ? 1u : 0u);
  return CharacterReadbackResult::kSuccess;
}

void MarkCharacterPreviewTransientlyUnavailable() {
  // The bridge already owns the last submitted RGBA frame. Do not replace a
  // valid portrait with "syncing" because one Present raced a cell transition,
  // a renderer reset, or a pending clone adoption.
  if (!g_character_render.has_good_frame && g_set_character_ready)
    g_set_character_ready(0);
}

void TickCharacterPreview(IDXGISwapChain* chain) {
  if (!g_native_character_preview_enabled) return;
  if (!g_get_character_surface || !g_submit_character_preview) return;
  dis::CharacterPreviewSurface control{};
  const bool visible = g_get_character_surface(&control) != 0;
  static int last_surface_state = -1;
  static bool surface_was_valid_visible = false;
  const int surface_state = visible ? 1 : 0;
  if (surface_state != last_surface_state) {
    PluginLog(false,
              "character-preview: surface state visible=%u version=%08X "
              "size_field=%u viewport=%dx%d rotation=%.1f zoom=%.2f "
              "control_revision=%llu",
              visible ? 1u : 0u, control.version, control.size,
              control.width, control.height, control.rotation_degrees,
              control.zoom,
              static_cast<unsigned long long>(control.control_revision));
    last_surface_state = surface_state;
  }
  if (!visible) {
    g_character_surface_visible.store(false, std::memory_order_release);
    g_character_interacting.store(false, std::memory_order_release);
    surface_was_valid_visible = false;
    if (g_set_character_ready) g_set_character_ready(0);
    // A page switch can race the Present which completes the one-shot actor
    // readback. Finalize a successful frame into the CPU cache even while
    // hidden, then retire the shallow Skyrim graph after the ordered GPU event.
    // Never make scene-object lifetime depend on revisiting the character page.
    ConsumeCompletedCharacterReadback(false);
    if (CharacterCloneLifecycleQuiescent() &&
        (g_character_render.active_clone || PendingCharacterPointer() ||
         g_character_render.scheme_session_owned)) {
      void* manager = ReadOr<void*>(
          reinterpret_cast<void*>(g_base + kUI3DSceneManagerPtr), nullptr);
      RetireCharacterSceneObjects(manager, "character surface hidden", true);
    }
    return;
  }
  if (control.version != dis::kBridgeVersion ||
      control.size != sizeof(control)) {
    g_character_surface_visible.store(false, std::memory_order_release);
    g_character_interacting.store(false, std::memory_order_release);
    // Do not consume the hidden-to-visible transition until the proxy has
    // supplied a valid surface. Otherwise the retained frame remains cleared
    // after a transient bridge/version mismatch and the UI stays "syncing".
    surface_was_valid_visible = false;
    MarkCharacterPreviewTransientlyUnavailable();
    return;
  }
  g_character_surface_visible.store(true, std::memory_order_release);
  g_character_interacting.store((control.reserved & 1u) != 0,
                                std::memory_order_release);
  const uint64_t completed_signature =
      g_character_render.readback_appearance_signature;
  const uint64_t completed_source_revision =
      g_character_render.readback_source_revision;
  const CharacterReadbackResult completed_readback =
      ConsumeCompletedCharacterReadback(true);
  if (completed_readback == CharacterReadbackResult::kEmpty) {
    constexpr uint32_t kMaximumConsecutiveEmptyFrames = 30;
    ++g_character_render.consecutive_empty_frames;
    if (g_character_render.consecutive_empty_frames >=
        kMaximumConsecutiveEmptyFrames) {
      g_character_render.empty_render_suspended = true;
      g_character_empty_suspended_signature.store(
          completed_signature, std::memory_order_release);
      PluginLog(false,
                "character-preview: suspended empty UI3D scene after %u "
                "consecutive frames; retry requires page revisit or a new "
                "appearance clone source_revision=%llu scene=%u/%u/%u",
                g_character_render.consecutive_empty_frames,
                static_cast<unsigned long long>(completed_source_revision),
                g_character_render.scene_objects,
                g_character_render.geometry_objects,
                g_character_render.bounded_objects);
    } else if (!PendingCharacterPointer()) {
      // Present already retired the submitted shallow clone. Ask the owning
      // player-update thread for another stable standing sample; never keep an
      // actor graph alive merely to retry an empty GPU result.
      g_character_clone_retry_requested.store(true,
                                                std::memory_order_release);
    }
    MarkCharacterPreviewTransientlyUnavailable();
  } else if (completed_readback == CharacterReadbackResult::kStale) {
    if (!g_character_render.has_good_frame &&
        !g_character_render.active_clone && !PendingCharacterPointer()) {
      g_character_clone_retry_requested.store(true,
                                                std::memory_order_release);
    }
    MarkCharacterPreviewTransientlyUnavailable();
  }
  const bool became_valid_visible = !surface_was_valid_visible;
  surface_was_valid_visible = true;
  if (became_valid_visible && !g_character_render.has_good_frame) {
    // A page revisit is an explicit, bounded retry after an empty UI3D render
    // was quarantined. This avoids a permanent failure while still preventing
    // an empty scene from rendering every Present for the rest of the session.
    g_character_render.consecutive_empty_frames = 0;
    g_character_render.empty_render_suspended = false;
    g_character_empty_suspended_signature.store(0,
                                                 std::memory_order_release);
    if (!g_character_render.active_clone && !PendingCharacterPointer())
      g_character_clone_retry_requested.store(true,
                                                std::memory_order_release);
  }
  if (g_character_render.has_good_frame &&
      g_character_render.cached_world_epoch !=
          g_world_epoch.load(std::memory_order_acquire)) {
    g_character_render.has_good_frame = false;
    g_character_render.cached_world_epoch = 0;
    g_character_render.cached_source_revision = 0;
    g_character_render.cached_appearance_signature = 0;
    g_character_cached_signature.store(0, std::memory_order_release);
    g_character_cached_world_epoch.store(0, std::memory_order_release);
    if (g_set_character_ready) g_set_character_ready(0);
  }
  if (became_valid_visible &&
      completed_readback != CharacterReadbackResult::kSuccess &&
      g_character_render.has_good_frame &&
      g_character_render.output_width && g_character_render.output_height &&
      g_character_render.cached_world_epoch ==
          g_world_epoch.load(std::memory_order_acquire) &&
      LiveWorldReadyForNativeRendering()) {
    // ready=0 intentionally clears the proxy's cached frame. Republish our
    // retained CPU copy before making the preview visible again.
    // Publish readiness before the frame. The proxy deliberately rejects
    // submissions while readiness is false, closing the late-frame window
    // after a hidden page or renderer reset. A cleared cache may display the
    // lightweight syncing state for only this bridge copy.
    if (g_set_character_ready) g_set_character_ready(1);
    g_submit_character_preview(g_character_render.output_width,
                               g_character_render.output_height,
                               g_character_frame_rgba,
                               g_character_render.output_width * 4,
                               g_character_render.cached_source_revision);
    if (g_character_render.cached_world_epoch ==
            g_world_epoch.load(std::memory_order_acquire) &&
        LiveWorldReadyForNativeRendering()) {
      if (g_set_character_ready) g_set_character_ready(1);
    } else {
      g_character_render.has_good_frame = false;
      if (g_set_character_ready) g_set_character_ready(0);
      return;
    }
  }
  const uint64_t now = GetTickCount64();
  if (g_character_transition_pending.load(std::memory_order_acquire) ||
      NativeInventoryMenuOwnsUi3D(now)) {
    // Keep the cached RGBA portrait, but retire any transient Skyrim graph as
    // soon as its Present query is genuinely complete. InventoryMenu can then
    // rebuild equipment without sharing skin/controller objects with WEMU.
    if (CharacterCloneLifecycleQuiescent() &&
        (g_character_render.active_clone || PendingCharacterPointer() ||
         g_character_render.scheme_session_owned)) {
      void* manager = ReadOr<void*>(
          reinterpret_cast<void*>(g_base + kUI3DSceneManagerPtr), nullptr);
      RetireCharacterSceneObjects(manager, "native InventoryMenu open", true);
    }
    return;
  }
  if (g_character_render.empty_render_suspended) return;
  // A portrait at rest is event-driven and normally performs no GPU work.
  // While the user is dragging, however, a pending control revision can use a
  // 30 FPS cap so rotation feels continuous. It still submits at most one
  // asynchronous readback at a time and immediately returns to zero-cost
  // cached presentation when the gesture stops.
  const bool interactive_control_update =
      g_character_render.active_clone &&
      g_character_render.last_control_revision != control.control_revision;
  const bool live_equipment_transition =
      now < g_character_live_transition_until_ms.load(
                std::memory_order_acquire);
  const uint64_t render_interval_ms =
      (interactive_control_update || live_equipment_transition) ? 33 : 100;
  if (now - g_character_render.last_render_ms < render_interval_ms) return;
  // Advance the limiter before attempting the optional render. Previously an
  // empty/failed frame returned without updating this timestamp, so the hook
  // retried every Present (thousands of UI3D renders) instead of at 10 FPS.
  g_character_render.last_render_ms = now;

  void* manager = ReadOr<void*>(
      reinterpret_cast<void*>(g_base + kUI3DSceneManagerPtr), nullptr);
  const bool manager_valid = manager && RegionAllows(manager, 0x118);
  void* pending_clone = PendingCharacterPointer();
  const bool has_character_work =
      pending_clone || g_character_render.active_clone;
  // Scheme initialization changes UI3DSceneManager's process-global current
  // light scheme. Do not initialize our private scene merely because the HUD
  // is visible; wait until a stable standing clone actually exists.
  if (!has_character_work) {
    if (g_character_render.scheme_session_owned &&
        CharacterCloneLifecycleQuiescent()) {
      RetireCharacterSceneObjects(manager, "cached frame scheme cleanup",
                                  true);
    }
    if (g_character_render.has_good_frame &&
        (g_character_render.last_control_revision !=
             control.control_revision ||
         g_character_render.output_width !=
             static_cast<uint32_t>(control.width) ||
         g_character_render.output_height !=
             static_cast<uint32_t>(control.height))) {
      const bool new_request =
          g_character_render.refresh_request_control_revision !=
              control.control_revision ||
          g_character_render.refresh_request_width != control.width ||
          g_character_render.refresh_request_height != control.height;
      if (new_request) {
        g_character_render.refresh_request_control_revision =
            control.control_revision;
        g_character_render.refresh_request_width = control.width;
        g_character_render.refresh_request_height = control.height;
        g_character_clone_retry_requested.store(true,
                                                  std::memory_order_release);
        PluginLog(false,
                  "[CHAR_PREVIEW] cached-frame control changed; fresh "
                  "transient clone requested revision=%llu size=%dx%d",
                  static_cast<unsigned long long>(control.control_revision),
                  control.width, control.height);
      }
    }
    MarkCharacterPreviewTransientlyUnavailable();
    return;
  }
  // Present already detached the retained interaction clone. If the finger is
  // still down but no new rotation/zoom revision arrived, keep it detached and
  // do zero GPU work. Reattaching and immediately taking the static-retirement
  // branch would destroy the gesture clone between touch-move packets and
  // recreate the original jitter.
  if (g_character_interacting.load(std::memory_order_acquire) &&
      g_character_render.active_clone && !g_character_render.attached &&
      g_character_render.has_good_frame &&
      g_character_render.last_control_revision == control.control_revision &&
      g_character_render.output_width ==
          static_cast<uint32_t>(control.width) &&
      g_character_render.output_height ==
          static_cast<uint32_t>(control.height)) {
    return;
  }
  bool scheme_ready =
      manager_valid && g_character_render.scheme_session_owned &&
      manager == g_character_render.scheme_manager &&
      Ui3DSchemeStackContains(manager, kCharacterPreviewScheme);
  if (!scheme_ready && manager_valid)
    scheme_ready = AcquireCharacterUi3DScheme(manager);
  if (!manager_valid || !scheme_ready ||
      !AdoptPendingCharacter(manager)) {
    MarkCharacterPreviewTransientlyUnavailable();
    if (now - g_character_render.last_failure_log_ms >= 5000) {
      const uint32_t scheme = manager && RegionAllows(manager, 0x94)
                                  ? ReadOr<uint32_t>(
                                        reinterpret_cast<uint8_t*>(manager) +
                                            0x90,
                                        UINT32_MAX)
                                  : UINT32_MAX;
      void* camera = manager && RegionAllows(manager, 0x28)
                         ? ReadOr<void*>(
                               reinterpret_cast<uint8_t*>(manager) + 0x20,
                               nullptr)
                         : nullptr;
      PluginLog(false,
                "character-preview: renderer prerequisites missing "
                "manager=%p valid=%u camera=%p scheme=%u pending_clone=%p "
                "active_clone=%p reset_revision=%llu source_revision=%llu",
                manager,
                manager && RegionAllows(manager, 0x118) ? 1u : 0u, camera,
                scheme,
                pending_clone,
                g_character_render.active_clone,
                static_cast<unsigned long long>(
                    g_character_reset_revision.load(std::memory_order_acquire)),
                static_cast<unsigned long long>(
                    g_character_source_revision.load(std::memory_order_acquire)));
      g_character_render.last_failure_log_ms = now;
    }
    return;
  }
  // The world can enter teardown after the Present hook's outer gate or while
  // a pending clone is being adopted. Check again immediately before calling
  // Skyrim's UI3D renderer; rendering an actor graph from the prior epoch is
  // substantially riskier than dropping one preview frame.
  if (g_character_render.active_world_epoch !=
          g_world_epoch.load(std::memory_order_acquire) ||
      !LiveWorldReadyForNativeRendering()) {
    MarkCharacterPreviewTransientlyUnavailable();
    return;
  }
  // A static portrait does not need another GPU pass until its actual inputs
  // change. Appearance changes publish a replacement clone above; touch
  // rotation/zoom changes increment control_revision; panel resizes change the
  // requested dimensions. Retaining the native result between those events
  // both freezes the idle pose and removes the old continuous 10 FPS UI3D
  // cost from normal gameplay.
  if (g_character_render.has_good_frame &&
      g_character_render.pose_initialized &&
      g_character_render.last_control_revision == control.control_revision &&
      g_character_render.output_width ==
          static_cast<uint32_t>(control.width) &&
      g_character_render.output_height ==
          static_cast<uint32_t>(control.height)) {
    if ((g_character_render.active_clone || PendingCharacterPointer() ||
         g_character_render.scheme_session_owned) &&
        !RetireCharacterSceneObjects(manager,
                                     "static cached-frame retirement retry",
                                     true))
      return;
    return;
  }
  // The native menu can be pushed after the earlier ownership check while a
  // pending clone is being adopted. Close that race immediately before the
  // call which arms Skyrim's process-global UI3D framebuffer lease.
  if (NativeInventoryMenuOwnsUi3D(GetTickCount64())) {
    if (CharacterCloneLifecycleQuiescent())
      RetireCharacterSceneObjects(manager,
                                  "native InventoryMenu pre-render race",
                                  true);
    return;
  }
  const bool synchronous_result =
      RenderCharacterOffscreen(chain, manager, control);
  // This renderer is intentionally asynchronous: a successful dispatch keeps
  // the private target leased only until Present. The actor graph remains
  // alive until the ordered readback event is complete; a later Present maps
  // it and retires the scene. A future synchronous implementation may return
  // true, but it still must publish through ConsumeCompletedCharacterReadback.
  if (g_character_render.engine_render_result == 2 || synchronous_result)
    return;

  g_character_render.consecutive_empty_frames = 0;
  MarkCharacterPreviewTransientlyUnavailable();
  if (now - g_character_render.last_failure_log_ms >= 5000) {
    PluginLog(false,
              "character-preview: off-screen dispatch unavailable clone=%p "
              "size=%dx%d scene=%u/%u/%u engine_result=%u attempts=%llu "
              "success=%llu empty=%llu",
              g_character_render.active_clone, control.width, control.height,
              g_character_render.scene_objects,
              g_character_render.geometry_objects,
              g_character_render.bounded_objects,
              g_character_render.engine_render_result,
              static_cast<unsigned long long>(
                  g_character_render.render_attempts),
              static_cast<unsigned long long>(
                  g_character_render.render_successes),
              static_cast<unsigned long long>(
                  g_character_render.render_empty_frames));
    g_character_render.last_failure_log_ms = now;
  }
}

void ReleaseNativeMapCaptureResources() {
  if (g_map_capture.occlusion_query) g_map_capture.occlusion_query->Release();
  if (g_map_capture.pipeline_query) g_map_capture.pipeline_query->Release();
  if (g_map_capture.readback_query) g_map_capture.readback_query->Release();
  if (g_map_capture.staging) g_map_capture.staging->Release();
  if (g_map_capture.depth_view) g_map_capture.depth_view->Release();
  if (g_map_capture.depth_texture) g_map_capture.depth_texture->Release();
  if (g_map_capture.shader_view) g_map_capture.shader_view->Release();
  if (g_map_capture.render_view) g_map_capture.render_view->Release();
  if (g_map_capture.render_texture) g_map_capture.render_texture->Release();
  if (g_map_capture.device) g_map_capture.device->Release();
  g_map_capture.device = nullptr;
  g_map_capture.render_texture = nullptr;
  g_map_capture.render_view = nullptr;
  g_map_capture.shader_view = nullptr;
  g_map_capture.depth_texture = nullptr;
  g_map_capture.depth_view = nullptr;
  g_map_capture.staging = nullptr;
  g_map_capture.readback_query = nullptr;
  g_map_capture.pipeline_query = nullptr;
  g_map_capture.occlusion_query = nullptr;
  g_map_capture.chain = nullptr;
  g_map_capture.capture_width = 0;
  g_map_capture.capture_height = 0;
  g_map_capture.width = 0;
  g_map_capture.height = 0;
  g_map_capture.format = DXGI_FORMAT_UNKNOWN;
  g_map_capture.active_world_epoch = 0;
  g_map_capture.readback_world_epoch = 0;
  g_map_capture.readback_world_id = 0;
  g_map_capture.readback_request_id = 0;
  g_map_capture.readback_control_revision = 0;
  g_map_capture.readback_dispatched_ms = 0;
  g_map_capture.readback_dispatched_us = 0;
  g_map_capture.last_setup_us = 0;
  g_map_capture.last_render_us = 0;
  g_map_capture.last_pixel_hash = 0;
  g_map_capture.projected_marker_count = 0;
  for (auto& marker : g_map_capture.projected_markers) marker = {};
  g_map_capture.readback_pending = false;
  g_map_capture.readback_copy_issued = false;
}

bool EnsureNativeMapCaptureResources(IDXGISwapChain* chain,
                                     ID3D11Device* device,
                                     const D3D11_TEXTURE2D_DESC& source) {
  const bool format_ok =
      source.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
      source.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
      source.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
      source.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
  if (!chain || !device || !format_ok || source.SampleDesc.Count != 1 ||
      source.Width < 320 || source.Height < 240) return false;
  constexpr uint32_t kMapWidth = 960;
  constexpr uint32_t kMapHeight = 540;
  if (g_map_capture.render_texture && g_map_capture.render_view &&
      g_map_capture.shader_view && g_map_capture.depth_texture &&
      g_map_capture.depth_view && g_map_capture.staging &&
      g_map_capture.readback_query && g_map_capture.device == device &&
      g_map_capture.chain == chain &&
      g_map_capture.capture_width == kMapWidth &&
      g_map_capture.capture_height == kMapHeight &&
      g_map_capture.width == kMapWidth &&
      g_map_capture.height == kMapHeight &&
      g_map_capture.format == source.Format) return true;

  ReleaseNativeMapCaptureResources();
  D3D11_TEXTURE2D_DESC render_desc = {};
  render_desc.Width = kMapWidth;
  render_desc.Height = kMapHeight;
  render_desc.MipLevels = 1;
  render_desc.ArraySize = 1;
  render_desc.Format = source.Format;
  render_desc.SampleDesc.Count = 1;
  render_desc.Usage = D3D11_USAGE_DEFAULT;
  render_desc.BindFlags =
      D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  HRESULT result = device->CreateTexture2D(
      &render_desc, nullptr, &g_map_capture.render_texture);
  if (SUCCEEDED(result))
    result = device->CreateRenderTargetView(
        g_map_capture.render_texture, nullptr, &g_map_capture.render_view);
  if (SUCCEEDED(result))
    result = device->CreateShaderResourceView(
        g_map_capture.render_texture, nullptr, &g_map_capture.shader_view);

  D3D11_TEXTURE2D_DESC depth_desc = {};
  depth_desc.Width = kMapWidth;
  depth_desc.Height = kMapHeight;
  depth_desc.MipLevels = 1;
  depth_desc.ArraySize = 1;
  depth_desc.Format = DXGI_FORMAT_D32_FLOAT;
  depth_desc.SampleDesc.Count = 1;
  depth_desc.Usage = D3D11_USAGE_DEFAULT;
  depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
  if (SUCCEEDED(result))
    result = device->CreateTexture2D(
        &depth_desc, nullptr, &g_map_capture.depth_texture);
  if (SUCCEEDED(result))
    result = device->CreateDepthStencilView(
        g_map_capture.depth_texture, nullptr, &g_map_capture.depth_view);

  D3D11_TEXTURE2D_DESC staging_desc = render_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  staging_desc.MiscFlags = 0;
  if (SUCCEEDED(result))
    result = device->CreateTexture2D(
        &staging_desc, nullptr, &g_map_capture.staging);
  D3D11_QUERY_DESC query_desc = {};
  query_desc.Query = D3D11_QUERY_EVENT;
  if (SUCCEEDED(result))
    result = device->CreateQuery(&query_desc,
                                 &g_map_capture.readback_query);
  if (FAILED(result)) {
    PluginLog(false,
              "map-native: private surface creation failed hr=%08lX",
              static_cast<unsigned long>(result));
    ReleaseNativeMapCaptureResources();
    return false;
  }
  device->AddRef();
  D3D11_QUERY_DESC diagnostic_query = {};
  diagnostic_query.Query = D3D11_QUERY_PIPELINE_STATISTICS;
  device->CreateQuery(&diagnostic_query, &g_map_capture.pipeline_query);
  diagnostic_query.Query = D3D11_QUERY_OCCLUSION;
  device->CreateQuery(&diagnostic_query, &g_map_capture.occlusion_query);
  g_map_capture.device = device;
  g_map_capture.chain = chain;
  g_map_capture.capture_width = kMapWidth;
  g_map_capture.capture_height = kMapHeight;
  g_map_capture.width = kMapWidth;
  g_map_capture.height = kMapHeight;
  g_map_capture.format = source.Format;
  PluginLog(false,
            "[SKYRIM-MAP-RT] private surface ready primary=%ux%u "
            "private=%ux%u format=%u source_is_gameplay=0",
            source.Width, source.Height, kMapWidth, kMapHeight,
            static_cast<unsigned>(source.Format));
  return true;
}

class ScopedWorldMapSceneState {
 public:
  ~ScopedWorldMapSceneState() { Restore(); }

  bool Acquire() {
    if (active_) return false;
    // Never overlap Skyrim's real MapMenu transaction. The outer render hook
    // performs the same check, but repeating it here closes the race between
    // preflight and scene preparation.
    if (ReadOr<uint8_t>(
            reinterpret_cast<void*>(g_base + kWorldMapSceneActive), 0) != 0)
      return false;
    prepare_ = reinterpret_cast<PrepareFn>(
        g_base + kPrepareWorldMapScene);
    restore_ = reinterpret_cast<RestoreFn>(
        g_base + kRestoreWorldMapScene);
    if (!RegionAllows(reinterpret_cast<void*>(prepare_), 1, true) ||
        !RegionAllows(reinterpret_cast<void*>(restore_), 1, true))
      return false;
    prepare_();
    active_ = true;
    return true;
  }

  void Restore() {
    if (!active_) return;
    // False is vanilla's ordinary map-close path. It restores the exact cell
    // LOD and NiAVObject visibility changes made by Prepare without invoking
    // the broad gameplay-camera/weather teardown.
    restore_(false);
    active_ = false;
  }

 private:
  using PrepareFn = void(*)();
  using RestoreFn = void(*)(bool);
  PrepareFn prepare_ = nullptr;
  RestoreFn restore_ = nullptr;
  bool active_ = false;
};

// MapCamera does not transform world geometry.  The NiNode passed by MapMenu is
// WorldRoot's CameraNode, and Skyrim updates the NiCamera below that node by
// traversing the CameraNode's children.  The old isolated path made its copied
// CameraNode childless and tried to invert that node transform into a detached
// camera.  Culling/marker projection then saw one camera while the renderer's
// native camera hierarchy still described the gameplay view, producing a real
// private draw that looked exactly like the top screen.
//
// Keep a two-object private hierarchy instead: a pinned CameraNode copy whose
// sole child is a pinned NiCamera copy. MapCamera can run its unmodified
// CameraNode update and NiCamera::UpdateWorldData path, but every transform and
// matrix write lands in transaction-local storage. The current producer uses
// UI3D's lower scene boundary and passes the private NiCamera explicitly, so it
// must never replace BSSceneGraph::camera. That legacy write was required only
// by the removed recursive Main_RenderWorld producer and could expose the map
// camera to gameplay rendering between passes.
class ScopedPrivateWorldMapObjects {
 public:
  ~ScopedPrivateWorldMapObjects() { Restore(); }

  bool Acquire(void* scene_graph, void* source_root, void* source_camera) {
    if (active_ || !scene_graph || !source_root || !source_camera ||
        !RegionAllows(source_root, kRootSize) ||
        !RegionAllows(source_camera, kCameraSize) ||
        !RegionAllows(reinterpret_cast<uint8_t*>(scene_graph) +
                          kSceneCameraOffset,
                      sizeof(void*)) ||
        !RegionWritable(reinterpret_cast<void*>(
                            g_base + kCurrentWorldRenderCamera),
                        sizeof(void*)) ||
        !RegionWritable(reinterpret_cast<void*>(
                            g_base + kShaderSceneCamera),
                        sizeof(void*)))
      return false;

    void* const scene_camera = ReadOr<void*>(
        reinterpret_cast<uint8_t*>(scene_graph) + kSceneCameraOffset,
        nullptr);
    published_camera_ = ReadOr<void*>(
        reinterpret_cast<void*>(g_base + kCurrentWorldRenderCamera), nullptr);
    shader_camera_ = ReadOr<void*>(
        reinterpret_cast<void*>(g_base + kShaderSceneCamera), nullptr);
    if (scene_camera != source_camera) return false;

    std::memcpy(root_storage_, source_root, kRootSize);
    std::memcpy(camera_storage_, source_camera, kCameraSize);
    std::memcpy(&source_root_world_,
                reinterpret_cast<uint8_t*>(source_root) + kWorldTransformOffset,
                sizeof(source_root_world_));
    std::memcpy(&source_camera_world_,
                reinterpret_cast<uint8_t*>(source_camera) +
                    kWorldTransformOffset,
                sizeof(source_camera_world_));
    std::memcpy(source_camera_matrix_,
                reinterpret_cast<uint8_t*>(source_camera) +
                    kWorldToCameraOffset,
                sizeof(source_camera_matrix_));
    constexpr LONG kPinnedReferenceCount = 0x100000;
    std::memcpy(root_storage_ + 0x08, &kPinnedReferenceCount,
                sizeof(kPinnedReferenceCount));
    std::memcpy(camera_storage_ + 0x08, &kPinnedReferenceCount,
                sizeof(kPinnedReferenceCount));

    // The native child list is a NiTObjectArray at +0x110. Keep its vtable but
    // replace the backing store and all four 16-bit counters so traversal can
    // reach only the private camera. No live CameraNode child is referenced.
    void* null_pointer = nullptr;
    std::memcpy(root_storage_ + 0x18, &null_pointer, sizeof(null_pointer));
    std::memcpy(root_storage_ + 0x30, &null_pointer, sizeof(null_pointer));
    std::memcpy(root_storage_ + 0x40, &null_pointer, sizeof(null_pointer));
    std::memcpy(root_storage_ + 0xF8, &null_pointer, sizeof(null_pointer));
    child_storage_[0] = camera_storage_;
    void** private_children = child_storage_;
    std::memcpy(root_storage_ + 0x118, &private_children,
                sizeof(private_children));
    const uint16_t one = 1;
    std::memcpy(root_storage_ + 0x120, &one, sizeof(one));
    std::memcpy(root_storage_ + 0x122, &one, sizeof(one));
    std::memcpy(root_storage_ + 0x124, &one, sizeof(one));
    std::memcpy(root_storage_ + 0x126, &one, sizeof(one));

    void* private_root = root_storage_;
    std::memcpy(camera_storage_ + 0x18, &null_pointer,
                sizeof(null_pointer));
    std::memcpy(camera_storage_ + 0x30, &private_root,
                sizeof(private_root));
    const uint32_t parent_index = 0;
    std::memcpy(camera_storage_ + 0x38, &parent_index,
                sizeof(parent_index));
    std::memcpy(camera_storage_ + 0x40, &null_pointer,
                sizeof(null_pointer));
    std::memcpy(camera_storage_ + 0xF8, &null_pointer,
                sizeof(null_pointer));

    active_ = true;
    return true;
  }

  bool ValidateNativeMapCameraUpdate() {
    if (!active_ ||
        !RegionAllows(root_storage_ + kWorldTransformOffset,
                      sizeof(SceneTransform)) ||
        !RegionAllows(camera_storage_ + kWorldTransformOffset,
                      sizeof(SceneTransform)) ||
        !RegionAllows(camera_storage_ + kWorldToCameraOffset,
                      sizeof(source_camera_matrix_)))
      return false;

    SceneTransform evaluated_root = {};
    std::memcpy(&evaluated_root, root_storage_ + kWorldTransformOffset,
                sizeof(evaluated_root));
    SceneTransform evaluated_camera = {};
    std::memcpy(&evaluated_camera,
                camera_storage_ + kWorldTransformOffset,
                sizeof(evaluated_camera));
    float rebuilt_matrix[16] = {};
    std::memcpy(rebuilt_matrix, camera_storage_ + kWorldToCameraOffset,
                sizeof(rebuilt_matrix));
    bool matrix_finite = true;
    for (float value : rebuilt_matrix)
      matrix_finite = matrix_finite && std::isfinite(value);
    const bool matrix_changed =
        std::memcmp(source_camera_matrix_, rebuilt_matrix,
                    sizeof(source_camera_matrix_)) != 0;
    const bool hierarchy_valid =
        child_storage_[0] == camera_storage_ &&
        ReadOr<void*>(camera_storage_ + 0x30, nullptr) == root_storage_ &&
        ReadOr<void**>(root_storage_ + 0x118, nullptr) == child_storage_;
    // The bottom-map camera is positioned above the exterior world. In
    // Skyrim's NiCamera convention rotate[6] is forward.z; it must be negative
    // for terrain below the camera to be inside the near/far frustum. Reject a
    // bad camera before scene traversal or presentation instead of accepting a
    // dark Sky-only frame as a successful map.
    const bool faces_world = evaluated_camera.rotate[6] < -0.95f;
    if (!TransformFinite(evaluated_root) ||
        !TransformFinite(evaluated_camera) || !matrix_finite ||
        !matrix_changed || !hierarchy_valid || !faces_world) {
      PluginLog(false,
                "[SKYRIM-MAP-CAMERA] rejected private camera "
                "matrix_changed=%u hierarchy_valid=%u forward_z=%.6f",
                matrix_changed ? 1u : 0u, hierarchy_valid ? 1u : 0u,
                evaluated_camera.rotate[6]);
      return false;
    }

    const uint32_t diagnostic =
        g_native_map_render_diagnostic_frames.load(std::memory_order_acquire);
    if (diagnostic < 3) {
      PluginLog(false,
                "[SKYRIM-MAP-CAMERA] native private hierarchy evaluated "
                "root_original=(%.1f,%.1f,%.1f s=%.5f) "
                "root_evaluated=(%.1f,%.1f,%.1f s=%.5f) "
                "camera_original=(%.1f,%.1f,%.1f) "
                "camera_evaluated=(%.1f,%.1f,%.1f s=%.5f) "
                "matrix_changed=%u hierarchy_valid=%u",
                source_root_world_.translate[0],
                source_root_world_.translate[1],
                source_root_world_.translate[2], source_root_world_.scale,
                evaluated_root.translate[0], evaluated_root.translate[1],
                evaluated_root.translate[2], evaluated_root.scale,
                source_camera_world_.translate[0],
                source_camera_world_.translate[1],
                source_camera_world_.translate[2],
                evaluated_camera.translate[0],
                evaluated_camera.translate[1],
                evaluated_camera.translate[2], evaluated_camera.scale,
                matrix_changed ? 1u : 0u,
                hierarchy_valid ? 1u : 0u);
      PluginLog(false,
                "[SKYRIM-MAP-CAMERA] basis "
                "forward=(%.5f,%.5f,%.5f) side=(%.5f,%.5f,%.5f) "
                "up=(%.5f,%.5f,%.5f) worldToCam.row3="
                "(%.5f,%.5f,%.5f,%.5f)",
                evaluated_camera.rotate[0], evaluated_camera.rotate[3],
                evaluated_camera.rotate[6], evaluated_camera.rotate[1],
                evaluated_camera.rotate[4], evaluated_camera.rotate[7],
                evaluated_camera.rotate[2], evaluated_camera.rotate[5],
                evaluated_camera.rotate[8], rebuilt_matrix[12],
                rebuilt_matrix[13], rebuilt_matrix[14], rebuilt_matrix[15]);
    }
    return true;
  }

  bool Restore() {
    if (!active_) return true;
    void* const current_published = ReadOr<void*>(
        reinterpret_cast<void*>(g_base + kCurrentWorldRenderCamera), nullptr);
    void* const current_shader = ReadOr<void*>(
        reinterpret_cast<void*>(g_base + kShaderSceneCamera), nullptr);
    const bool published_restored =
        current_published == published_camera_ ||
        WriteValue(reinterpret_cast<void*>(g_base + kCurrentWorldRenderCamera),
                   published_camera_);
    const bool shader_restored =
        current_shader == shader_camera_ ||
        WriteValue(reinterpret_cast<void*>(g_base + kShaderSceneCamera),
                   shader_camera_);
    if (!published_restored || !shader_restored) {
      PluginLog(true,
                "[MAP_OWNERSHIP] failed to restore renderer camera "
                "publication private=%p published=%p shader=%p",
                camera_storage_, published_camera_, shader_camera_);
      g_native_render_reset_revision.fetch_add(1,
                                                std::memory_order_acq_rel);
    }
    active_ = false;
    published_camera_ = nullptr;
    shader_camera_ = nullptr;
    return published_restored && shader_restored;
  }

  void* root() { return active_ ? root_storage_ : nullptr; }
  void* camera() { return active_ ? camera_storage_ : nullptr; }

 private:
  static bool TransformFinite(const SceneTransform& transform) {
    if (!std::isfinite(transform.scale) ||
        std::abs(transform.scale) < 0.000001f)
      return false;
    for (float value : transform.rotate)
      if (!std::isfinite(value)) return false;
    for (float value : transform.translate)
      if (!std::isfinite(value)) return false;
    return true;
  }

  static constexpr size_t kRootSize = 0x128;
  static constexpr size_t kCameraSize = 0x188;
  static constexpr size_t kSceneCameraOffset = 0x128;
  static constexpr size_t kWorldTransformOffset = 0x7C;
  static constexpr size_t kWorldToCameraOffset = 0x110;
  void* published_camera_ = nullptr;
  void* shader_camera_ = nullptr;
  bool active_ = false;
  SceneTransform source_root_world_ = {};
  SceneTransform source_camera_world_ = {};
  float source_camera_matrix_[16] = {};
  void* child_storage_[1] = {};
  alignas(16) uint8_t root_storage_[kRootSize] = {};
  alignas(16) uint8_t camera_storage_[kCameraSize] = {};
};

// BSSceneGraph's WorldRoot is not a valid UI3D subject. Its first child is the
// live gameplay CameraNode and its second child is the specialized
// `shadow scene node`; passing either parent through UI3D invokes renderer
// callbacks that own the gameplay camera/shadow pipeline. Device traces proved
// that the latter callback terminates inside shadow traversal.
//
// The renderable world objects are the children *below* that specialized
// parent. Build a non-owning, transaction-local NiNode facade over those
// children. Their evaluated world transforms, Skyrim geometry, materials and
// GPU resources remain authoritative, while culling starts at an ordinary
// NiNode and can never visit the gameplay CameraNode or shadow-scene callback.
class ScopedNativeMapSceneProxy {
 public:
  bool Acquire(void* world_root) {
    if (active_ || !world_root || !RegionAllows(world_root, kNodeSize))
      return false;

    const uint16_t world_child_count = ReadOr<uint16_t>(
        reinterpret_cast<uint8_t*>(world_root) + 0x124, 0);
    void** world_children = world_child_count
        ? ReadOr<void**>(reinterpret_cast<uint8_t*>(world_root) + 0x118,
                         nullptr)
        : nullptr;
    if (!world_children || !RegionAllows(world_children,
                                         sizeof(void*) * world_child_count))
      return false;

    void* render_parent = nullptr;
    for (uint16_t index = 0; index < world_child_count; ++index) {
      void* child = ReadOr<void*>(world_children + index, nullptr);
      if (!child || !RegionAllows(child, kNodeSize)) continue;
      const char* name = Ui3DObjectName(child);
      if (name && std::strstr(name, "shadow scene node")) {
        render_parent = child;
        break;
      }
    }
    if (!render_parent) return false;

    const uint16_t source_child_count = ReadOr<uint16_t>(
        reinterpret_cast<uint8_t*>(render_parent) + 0x124, 0);
    void** source_children = source_child_count
        ? ReadOr<void**>(reinterpret_cast<uint8_t*>(render_parent) + 0x118,
                         nullptr)
        : nullptr;
    if (!source_children || source_child_count == 0 ||
        source_child_count > child_storage_.size() ||
        !RegionAllows(source_children, sizeof(void*) * source_child_count))
      return false;

    uint16_t proxy_child_count = 0;
    for (uint16_t index = 0; index < source_child_count; ++index) {
      void* child = ReadOr<void*>(source_children + index, nullptr);
      if (!child || !RegionAllows(child, sizeof(void*))) continue;
      // World-map geometry is below the same shadow-scene parent as the live
      // sky and weather branches. Those two branches are gameplay-camera
      // effects, not map geometry; including them made an upward-facing map
      // camera produce a technically non-empty but effectively black frame.
      // Keep the proxy limited to terrain/LOD/statics so a valid map frame
      // proves that actual world geometry reached the accumulator.
      const char* name = Ui3DObjectName(child);
      if (name && (!std::strcmp(name, "Sky") ||
                   !std::strcmp(name, "Weather")))
        continue;
      child_storage_[proxy_child_count++] = child;
    }
    if (!proxy_child_count) return false;

    std::memcpy(storage_, world_root, kNodeSize);
    constexpr LONG kPinnedReferenceCount = 0x100000;
    std::memcpy(storage_ + 0x08, &kPinnedReferenceCount,
                sizeof(kPinnedReferenceCount));
    void* null_pointer = nullptr;
    std::memcpy(storage_ + 0x30, &null_pointer, sizeof(null_pointer));
    std::memcpy(storage_ + 0x40, &null_pointer, sizeof(null_pointer));
    std::memcpy(storage_ + 0xF8, &null_pointer, sizeof(null_pointer));
    void** private_children = child_storage_.data();
    std::memcpy(storage_ + 0x118, &private_children,
                sizeof(private_children));
    std::memcpy(storage_ + 0x120, &proxy_child_count,
                sizeof(proxy_child_count));
    std::memcpy(storage_ + 0x122, &proxy_child_count,
                sizeof(proxy_child_count));
    std::memcpy(storage_ + 0x124, &proxy_child_count,
                sizeof(proxy_child_count));
    const uint16_t grow_by = 1;
    std::memcpy(storage_ + 0x126, &grow_by, sizeof(grow_by));
    source_parent_ = render_parent;
    child_count_ = proxy_child_count;
    active_ = true;
    return true;
  }

  void* root() { return active_ ? storage_ : nullptr; }
  void* source_parent() const { return source_parent_; }
  uint16_t child_count() const { return child_count_; }

 private:
  static constexpr size_t kNodeSize = 0x128;
  void* source_parent_ = nullptr;
  uint16_t child_count_ = 0;
  bool active_ = false;
  std::array<void*, 64> child_storage_{};
  alignas(16) uint8_t storage_[kNodeSize] = {};
};

class ScopedUi3DSceneRootLease {
 public:
  ~ScopedUi3DSceneRootLease() { Restore(); }

  bool Acquire(void* manager, int32_t scheme, void* root) {
    if (active_ || !manager || !root || scheme <= 0 || scheme >= 8)
      return false;
    slot_ = reinterpret_cast<void**>(
        reinterpret_cast<uint8_t*>(manager) + 0x38 +
        static_cast<size_t>(scheme) * sizeof(void*));
    if (!RegionWritable(slot_, sizeof(*slot_))) {
      slot_ = nullptr;
      return false;
    }
    original_ = ReadOr<void*>(slot_, nullptr);
    if (!WriteValue(slot_, root)) {
      slot_ = nullptr;
      original_ = nullptr;
      return false;
    }
    active_ = true;
    return true;
  }

  bool Restore() {
    if (!active_) return true;
    const bool restored = slot_ && RegionWritable(slot_, sizeof(*slot_)) &&
                          WriteValue(slot_, original_);
    if (!restored) {
      PluginLog(true,
                "[MAP_OWNERSHIP] failed to restore UI3D map root "
                "slot=%p original=%p",
                slot_, original_);
      g_native_render_reset_revision.fetch_add(1,
                                                std::memory_order_acq_rel);
    }
    active_ = false;
    slot_ = nullptr;
    original_ = nullptr;
    return restored;
  }

 private:
  void** slot_ = nullptr;
  void* original_ = nullptr;
  bool active_ = false;
};

class ScopedNativeMapCamera {
 public:
  ~ScopedNativeMapCamera() { Destroy(); }

  bool Acquire(void* map_root) {
    if (constructed_ || !map_root || !RegionAllows(map_root, 0x110))
      return false;
    using ConstructorFn = void*(*)(void*, void*);
    using GetWorldSpaceFn = void*(*)();
    auto constructor = reinterpret_cast<ConstructorFn>(
        g_base + kMapCameraConstructor);
    auto get_world_space = reinterpret_cast<GetWorldSpaceFn>(
        g_base + kGetCurrentWorldSpace);
    if (!RegionAllows(reinterpret_cast<void*>(constructor), 1, true) ||
        !RegionAllows(reinterpret_cast<void*>(get_world_space), 1, true))
      return false;
    callbacks_vtable_ = reinterpret_cast<void*>(
        g_base + kNullMapCameraCallbacksVtable);
    if (!RegionAllows(callbacks_vtable_, sizeof(void*) * 3)) return false;
    if (constructor(storage_, &callbacks_vtable_) != storage_) return false;
    constructed_ = true;

    void* world_space = get_world_space();
    if (!world_space ||
        !WriteValue(storage_ + 0x50, world_space))
      return false;

    const bool anchor_resolved =
        g_native_map_anchor_resolved.load(std::memory_order_acquire);
    float map_position[3] = {
        anchor_resolved
            ? g_native_map_resolved_x.load(std::memory_order_acquire)
            : g_native_map_center_x.load(std::memory_order_acquire),
        anchor_resolved
            ? g_native_map_resolved_y.load(std::memory_order_acquire)
            : g_native_map_center_y.load(std::memory_order_acquire),
        anchor_resolved
            ? g_native_map_resolved_z.load(std::memory_order_acquire)
            : g_native_map_center_z.load(std::memory_order_acquire)};
    if (!std::isfinite(map_position[0]) ||
        !std::isfinite(map_position[1]) ||
        !std::isfinite(map_position[2]))
      return false;

    using ResolvePositionFn = bool(*)(void*, float*, void*);
    using WorldSetPositionFn = void(*)(void*, const float*);
    using SetCameraStateFn = void(*)(void*, void*);
    using UpdateFn = void(*)(void*, float);
    auto resolve_position = reinterpret_cast<ResolvePositionFn>(
        g_base + kMapCameraResolvePosition);
    auto set_world_position = reinterpret_cast<WorldSetPositionFn>(
        g_base + kMapCameraWorldSetPosition);
    auto set_camera_state = reinterpret_cast<SetCameraStateFn>(
        g_base + kCameraSetState);
    auto update = reinterpret_cast<UpdateFn>(g_base + kMapCameraUpdate);
    if (!RegionAllows(reinterpret_cast<void*>(resolve_position), 1, true) ||
        !RegionAllows(reinterpret_cast<void*>(set_world_position), 1, true) ||
        !RegionAllows(reinterpret_cast<void*>(set_camera_state), 1, true) ||
        !RegionAllows(reinterpret_cast<void*>(update), 1, true))
      return false;

    // MapMenu uses this exact position resolver and camera-root setup. Its
    // normal animated transition advances from Skyrim's global real-time
    // counter, not from the delta passed to MapCamera::Update. Because this
    // camera exists only for the isolated capture transaction, install the
    // World state which SetWorldRoot has already fully initialized rather than
    // waiting on a transition that cannot elapse inside one render callback.
    if (!anchor_resolved) {
      if (!resolve_position(storage_, map_position, map_root) ||
          !std::isfinite(map_position[0]) ||
          !std::isfinite(map_position[1]) ||
          !std::isfinite(map_position[2]))
        return false;
      g_native_map_resolved_x.store(map_position[0],
                                    std::memory_order_release);
      g_native_map_resolved_y.store(map_position[1],
                                    std::memory_order_release);
      g_native_map_resolved_z.store(map_position[2],
                                    std::memory_order_release);
      g_native_map_anchor_resolved.store(true, std::memory_order_release);
      PluginLog(false,
                "map-native: resolved independent anchor once world=%08X "
                "resolved=(%.1f,%.1f,%.1f)",
                g_native_map_world_id.load(std::memory_order_acquire),
                map_position[0], map_position[1], map_position[2]);
    }
    void* world_state = ActiveWorldState();
    if (!world_state || !RegionAllows(world_state, 0x74)) {
      PluginLog(false,
                "map-native: initialized world camera state unavailable "
                "active=%p current=%p",
                world_state, ReadOr<void*>(storage_ + 0x28, nullptr));
      return false;
    }
    // MapCamera::SetWorldRoot also hides the live PlayerCharacter node. Build
    // the same private World state directly so this preview camera never
    // touches player visibility or the gameplay camera.
    if (!WriteValue(storage_ + 0x20, map_root) ||
        !RegionWritable(storage_ + 0x40, sizeof(map_position)) ||
        !RegionWritable(reinterpret_cast<uint8_t*>(map_root) + 0xA0,
                        sizeof(map_position)))
      return false;
    std::memcpy(storage_ + 0x40, map_position, sizeof(map_position));
    std::memcpy(reinterpret_cast<uint8_t*>(map_root) + 0xA0, map_position,
                sizeof(map_position));
    set_world_position(world_state, map_position);
    set_camera_state(storage_, world_state);
    if (!ApplyIndependentTopDownView(update)) return false;
    return true;
  }

  void Destroy() {
    if (!constructed_) return;
    using DestructorFn = void(*)(void*);
    auto destructor = reinterpret_cast<DestructorFn>(
        g_base + kMapCameraDestructor);
    // MapMenu pairs MapCamera::SetPlayerVisibility(false/true) around its
    // *live* menu camera. This transaction never calls the hide half. Calling
    // only the restore half reads MapCamera+0x88 (constructor initializes it
    // to zero) and forcibly clears the real player's NiAVObject hidden flag.
    // That was a direct gameplay-scene write on every pan/zoom request and is
    // the source of the visible top-screen flash. Do not touch player
    // visibility at all for an isolated camera.
    if (RegionAllows(reinterpret_cast<void*>(destructor), 1, true))
      destructor(storage_);
    constructed_ = false;
  }

 private:
  using UpdateFn = void(*)(void*, float);

  void* ActiveWorldState() const {
    const uint32_t active = ReadOr<uint32_t>(
        const_cast<uint8_t*>(storage_) + 0x60, UINT32_MAX);
    if (active > 1) return nullptr;
    return ReadOr<void*>(const_cast<uint8_t*>(storage_) + 0x68 + active * 8,
                         nullptr);
  }

  bool ApplyIndependentTopDownView(UpdateFn update) {
    if (!update) return false;
    void* world_state = ActiveWorldState();
    void* current_state =
        ReadOr<void*>(storage_ + 0x28, nullptr);
    if (!world_state || current_state != world_state ||
        !RegionAllows(world_state, 0x74)) {
      PluginLog(false,
                "map-native: camera world-state install failed active=%p "
                "current=%p",
                world_state, current_state);
      return false;
    }

    auto* world = reinterpret_cast<uint8_t*>(world_state);
    const float original_pitch = ReadOr<float>(world + 0x54, 0.0f);
    const float initial_offset_x = ReadOr<float>(world + 0x2C, 0.0f);
    const float initial_offset_y = ReadOr<float>(world + 0x30, 0.0f);
    const float initial_offset_z = ReadOr<float>(world + 0x34, 0.0f);
    const float initial_zoom = ReadOr<float>(world + 0x58, 0.25f);
    // MapCameraStates::World::Update clamps this field to exactly +/-pi/2.
    // Skyrim's camera basis uses +pi/2 for a view whose forward Z is negative.
    // The previous -pi/2 value put the private camera above the world with
    // forward Z positive, so the near plane rejected the terrain below it and
    // only the Sky/Weather branches survived culling. Yaw remains canonical
    // north-up and the other native POD fields survive reconstruction of this
    // transaction-local camera, keeping it independent of PlayerCamera.
    constexpr float kTopDownPitch = 1.57079632679489661923f;
    const float zero = 0.0f;
    constexpr float kNorthUpYaw = 0.0f;
    bool view_initialized =
        g_native_map_view_initialized.load(std::memory_order_acquire);
    if (view_initialized) {
      if (!WriteValue(world + 0x2C,
                      g_native_map_view_offset_x.load(
                          std::memory_order_acquire)) ||
          !WriteValue(world + 0x30,
                      g_native_map_view_offset_y.load(
                          std::memory_order_acquire)) ||
          !WriteValue(world + 0x34,
                      g_native_map_view_offset_z.load(
                          std::memory_order_acquire)) ||
          !WriteValue(world + 0x58,
                      std::clamp(g_native_map_view_zoom.load(
                                     std::memory_order_acquire),
                                 0.0f, 1.0f)))
        return false;
    }
    if (!WriteValue(world + 0x50, kNorthUpYaw) ||
        !WriteValue(world + 0x54, kTopDownPitch) ||
        !WriteValue(storage_ + 0x08, zero) ||
        !WriteValue(storage_ + 0x0C, zero) ||
        !WriteValue(storage_ + 0x18, zero) ||
        !WriteValue(storage_ + 0x10, zero) ||
        !WriteValue(storage_ + 0x14, zero) ||
        !WriteValue(storage_ + 0x1C, zero))
      return false;

    // Snapshot the revision before consuming the atomics. A command racing
    // this transaction may be consumed with an older revision (causing one
    // harmless follow-up), but can never be marked applied while its deltas
    // remain pending and become stranded.
    const uint64_t consume_revision =
        g_native_map_control_revision.load(std::memory_order_acquire);
    float pan_x = g_native_map_pending_pan_x.exchange(
        0.0f, std::memory_order_acq_rel);
    float pan_y = g_native_map_pending_pan_y.exchange(
        0.0f, std::memory_order_acq_rel);
    const float zoom_delta = g_native_map_pending_zoom.exchange(
        0.0f, std::memory_order_acq_rel);
    if (!std::isfinite(pan_x)) pan_x = 0.0f;
    if (!std::isfinite(pan_y)) pan_y = 0.0f;
    const float pending_pan_x = pan_x;
    const float pending_pan_y = pan_y;
    const float pan_length = std::sqrt(pan_x * pan_x + pan_y * pan_y);
    if (pan_length > 1.0f) {
      pan_x /= pan_length;
      pan_y /= pan_length;
      // World::Update intentionally caps one native movement vector at unit
      // length. Retain the unconsumed integrated drag for the next bounded
      // capture rather than advancing the revision while silently dropping it.
      AccumulateNativeMapInput(g_native_map_pending_pan_x,
                               pending_pan_x - pan_x, 8.0f);
      AccumulateNativeMapInput(g_native_map_pending_pan_y,
                               pending_pan_y - pan_y, 8.0f);
    }
    // Touch drag is already an integrated displacement. World::Update reads
    // +0x10/+0x14 without multiplying by dt and applies Skyrim's zoom-aware
    // pan scale and world bounds. Invert X so the map follows the finger;
    // Android Y already maps naturally to Skyrim's second pan axis.
    if (!WriteValue(storage_ + 0x10, -pan_x) ||
        !WriteValue(storage_ + 0x14, pan_y))
      return false;
    const float zoom_before =
        std::clamp(ReadOr<float>(world + 0x58, initial_zoom), 0.0f, 1.0f);
    float zoom_target = zoom_before;
    if (std::isfinite(zoom_delta)) {
      // A two-times pinch moves one quarter of the normalized native zoom
      // range. Skyrim still performs the actual integration and clamping:
      // World::Update computes z' = clamp(z - 2*input, 0, 1).
      zoom_target = std::clamp(zoom_before - zoom_delta * 0.25f,
                               0.0f, 1.0f);
    }
    const float native_zoom_input = (zoom_before - zoom_target) * 0.5f;
    if (!WriteValue(storage_ + 0x1C, native_zoom_input)) return false;

    update(storage_, 0.0f);
    // Reassert after the engine update so floating-point input integration
    // cannot leave the camera one epsilon short of the bird's-eye endpoint.
    if (!WriteValue(world + 0x50, kNorthUpYaw) ||
        !WriteValue(world + 0x54, kTopDownPitch))
      return false;
    const float offset_x = ReadOr<float>(world + 0x2C, initial_offset_x);
    const float offset_y = ReadOr<float>(world + 0x30, initial_offset_y);
    const float offset_z = ReadOr<float>(world + 0x34, initial_offset_z);
    const float zoom_after =
        std::clamp(ReadOr<float>(world + 0x58, zoom_target), 0.0f, 1.0f);
    if (!std::isfinite(offset_x) || !std::isfinite(offset_y) ||
        !std::isfinite(offset_z) || !std::isfinite(zoom_after))
      return false;
    g_native_map_view_offset_x.store(offset_x, std::memory_order_release);
    g_native_map_view_offset_y.store(offset_y, std::memory_order_release);
    g_native_map_view_offset_z.store(offset_z, std::memory_order_release);
    g_native_map_view_zoom.store(zoom_after, std::memory_order_release);
    g_native_map_view_initialized.store(true, std::memory_order_release);
    g_native_map_applied_control_revision.store(consume_revision,
                                                std::memory_order_release);

    static uint64_t last_camera_log_ms = 0;
    const uint64_t now = GetTickCount64();
    if (!last_camera_log_ms || now - last_camera_log_ms >= 3000) {
      PluginLog(false,
                "map-native: independent topdown state=%p pitch=%.6f->%.6f "
                "pan_input=(%.4f,%.4f) zoom=%.6f->%.6f "
                "offset=(%.1f,%.1f,%.1f) control_revision=%llu",
                world_state, original_pitch,
                ReadOr<float>(world + 0x54, 0.0f),
                pan_x, pan_y, zoom_before, zoom_after,
                offset_x, offset_y, offset_z,
                static_cast<unsigned long long>(consume_revision));
      last_camera_log_ms = now;
    }
    return true;
  }

  void* callbacks_vtable_ = nullptr;
  bool constructed_ = false;
  alignas(16) uint8_t storage_[0x90] = {};
};

uint32_t ProjectNativeMapMarkers(void* camera,
                                 dis::ProjectedMapMarker* projected,
                                 uint32_t capacity) {
  if (!camera || !projected || capacity == 0 ||
      !RegionAllows(camera, 0x188))
    return 0;
  using ProjectFn = bool(*)(void*, const float*, float*, float*, float*,
                            float);
  auto project = reinterpret_cast<ProjectFn>(g_base + kWorldPointToScreen);
  if (!RegionAllows(reinterpret_cast<void*>(project), 1, true)) return 0;

  uint32_t count = 0;
  auto append = [&](uint32_t form_id, uint32_t flags, uint16_t type,
                    float x, float y, float z, float rotation) {
    if (count >= capacity || (flags & dis::kMapMarkerVisible) == 0 ||
        !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        !std::isfinite(rotation))
      return;
    const float point[3] = {x, y, z};
    float screen_x = 0.0f;
    float screen_y = 0.0f;
    float depth = 0.0f;
    if (!project(camera, point, &screen_x, &screen_y, &depth, 1.0e-5f))
      return;
    // MapMenu::RefreshMarkers performs this exact Y conversion immediately
    // after NiCamera projection. Keep only markers vanilla considers visible.
    screen_y = 1.0f - screen_y;
    if (!std::isfinite(screen_x) || !std::isfinite(screen_y) ||
        !std::isfinite(depth) || screen_x < 0.0f || screen_x > 1.0f ||
        screen_y < 0.0f || screen_y > 1.0f || depth < 0.0f || depth > 1.0f)
      return;
    dis::ProjectedMapMarker& out = projected[count++];
    out = {};
    out.form_id = form_id;
    out.flags = flags;
    out.type = type;
    out.screen_x = screen_x;
    out.screen_y = screen_y;
    out.depth = depth;
    out.rotation_radians = rotation;
  };

  // Reserve one slot for YouAreHere so a large/modded location array can
  // never evict the player's native arrow. Reserve the remaining special-pin
  // slots before ordinary locations as well, then append pins last so their
  // artwork stays visible above a coincident location marker.
  const uint32_t location_capacity = capacity > 1 ? capacity - 1 : 0;
  AcquireSRWLockShared(&g_map_marker_cache_lock);
  const uint32_t source_count = std::min(g_map_marker_cache_count,
                                         dis::kMaxMapMarkers);
  uint32_t special_count = 0;
  for (uint32_t i = 0; i < source_count; ++i) {
    if (g_map_marker_cache[i].type >= 62) ++special_count;
  }
  const uint32_t special_budget = std::min(special_count, location_capacity);
  uint32_t normal_budget = location_capacity - special_budget;
  for (uint32_t i = 0; i < source_count && normal_budget; ++i) {
    const dis::MapMarkerState& marker = g_map_marker_cache[i];
    if (marker.type >= 62) continue;
    append(marker.form_id, marker.flags, marker.type,
           marker.x, marker.y, marker.z, 0.0f);
    --normal_budget;
  }
  uint32_t emitted_specials = 0;
  for (uint32_t i = 0;
       i < source_count && emitted_specials < special_budget; ++i) {
    const dis::MapMarkerState& marker = g_map_marker_cache[i];
    if (marker.type < 62) continue;
    append(marker.form_id, marker.flags, marker.type,
           marker.x, marker.y, marker.z, 0.0f);
    ++emitted_specials;
  }
  ReleaseSRWLockShared(&g_map_marker_cache_lock);

  append(0x00000014u, dis::kMapMarkerVisible, 66,
         g_native_map_player_x.load(std::memory_order_acquire),
         g_native_map_player_y.load(std::memory_order_acquire),
         g_native_map_player_z.load(std::memory_order_acquire),
         g_native_map_player_heading.load(std::memory_order_acquire));
  return count;
}

void LogNativeMapFailure(uint64_t now, const char* reason, HRESULT result,
                         void* world_root = nullptr,
                         void* world_camera = nullptr) {
  ++g_map_capture.failed_frames;
  g_native_map_failure_count.fetch_add(1, std::memory_order_relaxed);
  g_native_map_last_failure_reason.store(reason ? reason : "unknown",
                                         std::memory_order_release);
  g_native_map_last_failure_hr.store(static_cast<long>(result),
                                     std::memory_order_release);
  if (g_map_capture.last_failure_log_ms &&
      now - g_map_capture.last_failure_log_ms < 5000)
    return;
  PluginLog(false,
            "map-native: private render failure reason=%s hr=%08lX "
            "root=%p camera=%p target=%ux%u epoch=%llu world=%08X",
            reason ? reason : "unknown",
            static_cast<unsigned long>(result), world_root, world_camera,
            g_map_capture.width, g_map_capture.height,
            static_cast<unsigned long long>(
                g_world_epoch.load(std::memory_order_acquire)),
            g_native_map_world_id.load(std::memory_order_acquire));
  g_map_capture.last_failure_log_ms = now;
}

void FinalizeExpiredNativeMapRequest(uint64_t now) {
  const uint64_t deadline = g_native_map_requested_until_ms.load(
      std::memory_order_acquire);
  const uint64_t request = g_native_map_active_request.load(
      std::memory_order_acquire);
  if (!request || !deadline || now < deadline ||
      (g_map_capture.readback_pending &&
       g_map_capture.readback_request_id == request) ||
      g_native_map_terminal_request.load(std::memory_order_acquire) >= request)
    return;
  uint64_t prior = g_native_map_terminal_request.load(
      std::memory_order_acquire);
  if (prior >= request ||
      !g_native_map_terminal_request.compare_exchange_strong(
          prior, request, std::memory_order_acq_rel,
          std::memory_order_acquire))
    return;
  g_native_map_requested_until_ms.store(0, std::memory_order_release);
  const uint64_t started = g_native_map_request_started_ms.load(
      std::memory_order_acquire);
  const char* reason = g_native_map_last_failure_reason.load(
      std::memory_order_acquire);
  PluginLog(false,
            "[SKYRIM-MAP] request=%llu FAILURE phase=%s hr=%08lX "
            "elapsed_ms=%llu attempts_failed=%llu lock_misses=%llu",
            static_cast<unsigned long long>(request),
            reason ? reason : "not-dispatched",
            static_cast<unsigned long>(g_native_map_last_failure_hr.load(
                std::memory_order_acquire)),
            static_cast<unsigned long long>(started ? now - started : 0),
            static_cast<unsigned long long>(
                g_native_map_failure_count.load(std::memory_order_acquire) -
                g_native_map_request_failure_base.load(
                    std::memory_order_acquire)),
            static_cast<unsigned long long>(
                g_native_map_lock_misses.load(std::memory_order_acquire)));
}

class ScopedNativeMapFramebuffer {
 public:
  ~ScopedNativeMapFramebuffer() { Restore(); }

  bool Acquire(ID3D11DeviceContext* context) {
    if (active_ || !context || !g_map_capture.render_texture ||
        !g_map_capture.render_view || !g_map_capture.shader_view ||
        !g_map_capture.depth_view)
      return false;
    auto* target_manager = reinterpret_cast<uint8_t*>(
        g_base + kRenderTargetManager);
    auto* renderer = reinterpret_cast<uint8_t*>(
        g_base + kRendererSingleton);
    auto* special_renderer_data = reinterpret_cast<uint8_t*>(ReadOr<void*>(
        reinterpret_cast<void*>(g_base + kRendererDataPtr), nullptr));
    if (!special_renderer_data) return false;
    color_targets_ = reinterpret_cast<RenderTargetDataRaw*>(
        renderer + kRendererColorTargetDataOffset);
    framebuffer_ = color_targets_;
    depth_targets_ = reinterpret_cast<DepthStencilDataRaw*>(
        renderer + kRendererDepthTargetDataOffset);
    special_color_views_ = reinterpret_cast<ID3D11RenderTargetView**>(
        special_renderer_data + kRendererSpecialColorViewOffset);
    color_metadata_ = target_manager;
    depth_metadata_ = target_manager + kRendererDepthStencilWidthOffset;
    target_ids_ = reinterpret_cast<int32_t*>(
        g_base + kRendererActiveRenderTarget);
    target_modes_ = reinterpret_cast<int32_t*>(
        g_base + kRendererRenderTargetModes);
    depth_id_ = reinterpret_cast<int32_t*>(
        g_base + kRendererActiveDepthStencil);
    depth_mode_ = depth_id_ + 1;
    state_dirty_ = reinterpret_cast<uint32_t*>(
        g_base + kRendererStateDirtyFlags);
    if (!RegionWritable(framebuffer_, sizeof(*framebuffer_)) ||
        !RegionWritable(color_targets_,
                        sizeof(RenderTargetDataRaw) *
                            kRendererColorTargetCount) ||
        !RegionWritable(depth_targets_,
                        sizeof(DepthStencilDataRaw) *
                            kRendererDepthTargetCount) ||
        !RegionWritable(special_color_views_,
                        sizeof(void*) * kRendererSpecialColorViewCount) ||
        !RegionWritable(color_metadata_,
                        kRendererColorTargetCount *
                            kRendererTargetMetadataStride) ||
        !RegionWritable(depth_metadata_,
                        kRendererDepthTargetCount *
                            kRendererDepthMetadataStride) ||
        !RegionWritable(target_ids_, sizeof(saved_target_ids_)) ||
        !RegionWritable(target_modes_, sizeof(saved_target_modes_)) ||
        !RegionWritable(depth_id_, sizeof(*depth_id_)) ||
        !RegionWritable(depth_mode_, sizeof(*depth_mode_)) ||
        !RegionWritable(state_dirty_, sizeof(*state_dirty_)))
      return false;
    context_ = context;
    context_->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
                                 old_views_, &old_depth_);
    if (!old_views_[0]) {
      ReleaseCapturedComState();
      context_ = nullptr;
      return false;
    }
    context_->OMGetBlendState(&old_blend_, old_blend_factor_,
                              &old_sample_mask_);
    context_->OMGetDepthStencilState(&old_depth_state_,
                                     &old_stencil_reference_);
    context_->RSGetState(&old_rasterizer_);
    viewport_count_ = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    context_->RSGetViewports(&viewport_count_, old_viewports_);
    scissor_count_ = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    context_->RSGetScissorRects(&scissor_count_, old_scissors_);
    pipeline_state_.Capture(context_);

    saved_framebuffer_ = *framebuffer_;
    std::memcpy(saved_target_ids_, target_ids_, sizeof(saved_target_ids_));
    std::memcpy(saved_target_modes_, target_modes_,
                sizeof(saved_target_modes_));
    saved_depth_id_ = *depth_id_;
    saved_depth_mode_ = *depth_mode_;
    saved_state_dirty_ = *state_dirty_;

    auto patch_color_view = [&](ID3D11RenderTargetView** entry,
                                bool ordinary, uint32_t target) {
      if (!entry || !*entry || *entry == g_map_capture.render_view)
        return false;
      for (size_t i = 0; i < color_patch_count_; ++i) {
        if (color_patches_[i].entry == entry) return false;
      }
      if (color_patch_count_ >= color_patches_.size()) return false;
      color_patches_[color_patch_count_++] = {entry, *entry};
      *entry = g_map_capture.render_view;
      if (ordinary) {
        ++ordinary_color_patch_count_;
        PatchColorMetadata(target);
      } else {
        ++special_color_patch_count_;
      }
      return true;
    };

    // Redirect every ordinary target record which aliases the currently bound
    // primary gameplay RTV.  This is the exact record array consumed by
    // BSGraphics::ApplyRenderTargets; HDR, shadow, cube, and post-process
    // intermediates which do not alias the primary remain Skyrim-owned.
    for (uint32_t target = 0; target < kRendererColorTargetCount; ++target) {
      ID3D11RenderTargetView** entry = &color_targets_[target].render_view;
      if (*entry != old_views_[0]) continue;
      patch_color_view(entry, true, target);
    }
    // Main_RenderWorld re-applies the active target IDs before world
    // submission. kMAIN is normally distinct from the RTV that happened to be
    // bound at hook entry, so alias-only routing leaves every valid draw on
    // the gameplay texture. Redirect each active ordinary ID explicitly.
    for (uint32_t slot = 0;
         slot < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++slot) {
      const int32_t target = saved_target_ids_[slot];
      if (target < 0 ||
          static_cast<uint32_t>(target) >= kRendererColorTargetCount)
        continue;
      if (patch_color_view(&color_targets_[target].render_view, true,
                           static_cast<uint32_t>(target)))
        ++active_color_patch_count_;
    }
    // ApplyRenderTargets resolves cubemap/variant RTVs through the separate
    // RendererData pointer. Patch only aliases of the currently bound primary
    // view; unrelated reflections remain completely Skyrim-owned.
    for (uint32_t variant = 0;
         variant < kRendererSpecialColorViewCount; ++variant) {
      ID3D11RenderTargetView** entry = special_color_views_ + variant;
      if (*entry != old_views_[0]) continue;
      patch_color_view(entry, false, variant);
    }
    const int32_t special_target = ReadOr<int32_t>(reinterpret_cast<void*>(
        g_base + kRendererActiveRenderTarget + 0x28), -1);
    const int32_t special_mode = ReadOr<int32_t>(reinterpret_cast<void*>(
        g_base + kRendererActiveRenderTarget + 0x2C), -1);
    if (special_target == 0 && special_mode >= 0 &&
        static_cast<uint32_t>(special_mode) <
            kRendererSpecialColorViewCount &&
        patch_color_view(special_color_views_ + special_mode, false,
                         static_cast<uint32_t>(special_mode)))
      ++active_color_patch_count_;
    if (!color_patch_count_) {
      PluginLog(false,
                "[SKYRIM-MAP-RT] primary RTV absent from ordinary and special "
                "target records primary=%p private=%p active0=%d special=%d/%d",
                old_views_[0], g_map_capture.render_view,
                saved_target_ids_[0],
                special_target, special_mode);
      Restore();
      return false;
    }

    // Redirect only aliases of the gameplay depth view. Each 0x98-byte native
    // record contains eight writable and eight read-only DSVs; its texture and
    // SRVs are deliberately excluded from pointer patching.
    if (old_depth_) {
      auto patch_depth_view = [&](ID3D11DepthStencilView** entry) {
        if (!entry || !*entry || *entry == g_map_capture.depth_view)
          return false;
        for (size_t i = 0; i < depth_patch_count_; ++i) {
          if (depth_patches_[i].entry == entry) return false;
        }
        if (depth_patch_count_ >= depth_patches_.size()) return false;
        depth_patches_[depth_patch_count_++] = {entry, *entry};
        *entry = g_map_capture.depth_view;
        return true;
      };
      for (uint32_t target = 0; target < kRendererDepthTargetCount; ++target) {
        bool target_patched = false;
        for (uint32_t variant = 0;
             variant < kRendererDepthViewEntriesPerTarget; ++variant) {
          ID3D11DepthStencilView** entry = variant < 8
              ? &depth_targets_[target].writable_views[variant]
              : &depth_targets_[target].read_only_views[variant - 8];
          if (*entry != old_depth_) continue;
          target_patched |= patch_depth_view(entry);
        }
        if (target_patched) PatchDepthMetadata(target);
      }
    }

    // The hook commonly begins between passes with no DSV bound, then
    // Main_RenderWorld selects depth target zero. Route all non-null variants
    // of that active native record so its later mode/read-only changes cannot
    // pair a 1280x720 gameplay DSV with the private 960x540 color target.
    if (saved_depth_id_ >= 0 &&
        static_cast<uint32_t>(saved_depth_id_) < kRendererDepthTargetCount) {
      const uint32_t active_depth = static_cast<uint32_t>(saved_depth_id_);
      bool metadata_patched = false;
      for (uint32_t variant = 0;
           variant < kRendererDepthViewEntriesPerTarget; ++variant) {
        ID3D11DepthStencilView** entry = variant < 8
            ? &depth_targets_[active_depth].writable_views[variant]
            : &depth_targets_[active_depth].read_only_views[variant - 8];
        if (!*entry || *entry == g_map_capture.depth_view) continue;
        bool already_patched = false;
        for (size_t i = 0; i < depth_patch_count_; ++i) {
          if (depth_patches_[i].entry == entry) {
            already_patched = true;
            break;
          }
        }
        if (already_patched ||
            depth_patch_count_ >= depth_patches_.size())
          continue;
        depth_patches_[depth_patch_count_++] = {entry, *entry};
        *entry = g_map_capture.depth_view;
        ++active_depth_patch_count_;
        metadata_patched = true;
      }
      if (metadata_patched) PatchDepthMetadata(active_depth);
    }

    RenderTargetDataRaw private_target{};
    private_target.texture = g_map_capture.render_texture;
    private_target.texture_copy = g_map_capture.render_texture;
    private_target.render_view = g_map_capture.render_view;
    private_target.shader_view = g_map_capture.shader_view;
    private_target.shader_copy = g_map_capture.shader_view;
    *framebuffer_ = private_target;
    // Force Skyrim's state cache to reevaluate the redirected table on its
    // first native target change. The complete cache is restored below before
    // the gameplay pass executes.
    *state_dirty_ |= 1u;
    context_->OMSetRenderTargets(1, &g_map_capture.render_view,
                                 g_map_capture.depth_view);
    const D3D11_VIEWPORT viewport = {
        0.0f, 0.0f, static_cast<float>(g_map_capture.width),
        static_cast<float>(g_map_capture.height), 0.0f, 1.0f};
    context_->RSSetViewports(1, &viewport);
    const D3D11_RECT scissor = {
        0, 0, static_cast<LONG>(g_map_capture.width),
        static_cast<LONG>(g_map_capture.height)};
    context_->RSSetScissorRects(1, &scissor);
    active_ = true;
    PluginLog(false,
              "[SKYRIM-MAP-RT] LEASED primary_rtv=%p private_rtv=%p "
              "primary_dsv=%p private_dsv=%p ordinary_aliases=%llu "
              "special_aliases=%llu active_routes=%llu color_aliases=%llu "
              "depth_aliases=%llu active_depth_routes=%llu target0=%d "
              "depth=%d/%d size=%ux%u",
              old_views_[0], g_map_capture.render_view, old_depth_,
              g_map_capture.depth_view,
              static_cast<unsigned long long>(ordinary_color_patch_count_),
              static_cast<unsigned long long>(special_color_patch_count_),
              static_cast<unsigned long long>(active_color_patch_count_),
              static_cast<unsigned long long>(color_patch_count_),
              static_cast<unsigned long long>(depth_patch_count_),
              static_cast<unsigned long long>(active_depth_patch_count_),
              saved_target_ids_[0], saved_depth_id_, saved_depth_mode_,
              g_map_capture.width, g_map_capture.height);
    return true;
  }

  void Restore() {
    if (!context_) return;
    for (size_t i = color_patch_count_; i > 0; --i)
      *color_patches_[i - 1].entry = color_patches_[i - 1].value;
    for (size_t i = depth_patch_count_; i > 0; --i)
      *depth_patches_[i - 1].entry = depth_patches_[i - 1].value;
    for (size_t i = color_metadata_count_; i > 0; --i) {
      MetadataPatch& patch = color_metadata_patches_[i - 1];
      *patch.width = patch.old_width;
      *patch.height = patch.old_height;
    }
    for (size_t i = depth_metadata_count_; i > 0; --i) {
      MetadataPatch& patch = depth_metadata_patches_[i - 1];
      *patch.width = patch.old_width;
      *patch.height = patch.old_height;
    }
    *framebuffer_ = saved_framebuffer_;
    std::memcpy(target_ids_, saved_target_ids_, sizeof(saved_target_ids_));
    std::memcpy(target_modes_, saved_target_modes_,
                sizeof(saved_target_modes_));
    *depth_id_ = saved_depth_id_;
    *depth_mode_ = saved_depth_mode_;
    *state_dirty_ = saved_state_dirty_ | 1u;
    context_->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
                                 old_views_, old_depth_);
    context_->OMSetBlendState(old_blend_, old_blend_factor_, old_sample_mask_);
    context_->OMSetDepthStencilState(old_depth_state_,
                                     old_stencil_reference_);
    context_->RSSetState(old_rasterizer_);
    context_->RSSetViewports(viewport_count_,
                             viewport_count_ ? old_viewports_ : nullptr);
    context_->RSSetScissorRects(scissor_count_,
                                scissor_count_ ? old_scissors_ : nullptr);
    pipeline_state_.Restore(context_);
    ReleaseCapturedComState();
    color_patch_count_ = 0;
    ordinary_color_patch_count_ = 0;
    special_color_patch_count_ = 0;
    active_color_patch_count_ = 0;
    depth_patch_count_ = 0;
    active_depth_patch_count_ = 0;
    color_metadata_count_ = 0;
    depth_metadata_count_ = 0;
    context_ = nullptr;
    active_ = false;
  }

  size_t color_alias_count() const { return color_patch_count_; }
  size_t depth_alias_count() const { return depth_patch_count_; }

 private:
  template <typename T>
  struct PointerPatch {
    T** entry = nullptr;
    T* value = nullptr;
  };

  struct MetadataPatch {
    uint32_t* width = nullptr;
    uint32_t* height = nullptr;
    uint32_t old_width = 0;
    uint32_t old_height = 0;
  };

  void PatchColorMetadata(uint32_t target) {
    if (target >= kRendererColorTargetCount ||
        color_metadata_count_ >= color_metadata_patches_.size())
      return;
    auto* width = reinterpret_cast<uint32_t*>(
        color_metadata_ + static_cast<size_t>(target) *
                              kRendererTargetMetadataStride);
    auto* height = width + 1;
    MetadataPatch& patch =
        color_metadata_patches_[color_metadata_count_++];
    patch = {width, height, *width, *height};
    *width = g_map_capture.width;
    *height = g_map_capture.height;
  }

  void PatchDepthMetadata(uint32_t target) {
    if (target >= kRendererDepthTargetCount ||
        depth_metadata_count_ >= depth_metadata_patches_.size())
      return;
    auto* width = reinterpret_cast<uint32_t*>(
        depth_metadata_ + static_cast<size_t>(target) *
                              kRendererDepthMetadataStride);
    auto* height = width + 1;
    MetadataPatch& patch =
        depth_metadata_patches_[depth_metadata_count_++];
    patch = {width, height, *width, *height};
    *width = g_map_capture.width;
    *height = g_map_capture.height;
  }

  void ReleaseCapturedComState() {
    if (old_rasterizer_) old_rasterizer_->Release();
    if (old_depth_state_) old_depth_state_->Release();
    if (old_blend_) old_blend_->Release();
    if (old_depth_) old_depth_->Release();
    for (auto*& view : old_views_) {
      if (view) view->Release();
      view = nullptr;
    }
    old_rasterizer_ = nullptr;
    old_depth_state_ = nullptr;
    old_blend_ = nullptr;
    old_depth_ = nullptr;
  }

  ID3D11DeviceContext* context_ = nullptr;
  RenderTargetDataRaw* framebuffer_ = nullptr;
  RenderTargetDataRaw* color_targets_ = nullptr;
  DepthStencilDataRaw* depth_targets_ = nullptr;
  ID3D11RenderTargetView** special_color_views_ = nullptr;
  uint8_t* color_metadata_ = nullptr;
  uint8_t* depth_metadata_ = nullptr;
  int32_t* target_ids_ = nullptr;
  int32_t* target_modes_ = nullptr;
  int32_t* depth_id_ = nullptr;
  int32_t* depth_mode_ = nullptr;
  uint32_t* state_dirty_ = nullptr;
  RenderTargetDataRaw saved_framebuffer_{};
  int32_t saved_target_ids_[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
  int32_t saved_target_modes_[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
  int32_t saved_depth_id_ = -1;
  int32_t saved_depth_mode_ = -1;
  uint32_t saved_state_dirty_ = 0;
  std::array<PointerPatch<ID3D11RenderTargetView>,
             kRendererColorPatchCapacity>
      color_patches_{};
  std::array<PointerPatch<ID3D11DepthStencilView>,
             kRendererDepthTargetCount *
                 kRendererDepthViewEntriesPerTarget>
      depth_patches_{};
  std::array<MetadataPatch, kRendererColorTargetCount>
      color_metadata_patches_{};
  std::array<MetadataPatch, kRendererDepthTargetCount>
      depth_metadata_patches_{};
  size_t color_patch_count_ = 0;
  size_t ordinary_color_patch_count_ = 0;
  size_t special_color_patch_count_ = 0;
  size_t active_color_patch_count_ = 0;
  size_t depth_patch_count_ = 0;
  size_t active_depth_patch_count_ = 0;
  size_t color_metadata_count_ = 0;
  size_t depth_metadata_count_ = 0;
  ID3D11RenderTargetView* old_views_[
      D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
  ID3D11DepthStencilView* old_depth_ = nullptr;
  ID3D11BlendState* old_blend_ = nullptr;
  FLOAT old_blend_factor_[4] = {};
  UINT old_sample_mask_ = 0xFFFFFFFFu;
  ID3D11DepthStencilState* old_depth_state_ = nullptr;
  UINT old_stencil_reference_ = 0;
  ID3D11RasterizerState* old_rasterizer_ = nullptr;
  UINT viewport_count_ = 0;
  D3D11_VIEWPORT old_viewports_[
      D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
  UINT scissor_count_ = 0;
  D3D11_RECT old_scissors_[
      D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
  Ui3DPipelineState pipeline_state_;
  bool active_ = false;
};

bool RenderNativeMapPassBeforeGameplay(IDXGISwapChain* chain,
                                       MainRenderWorldFn render_world,
                                       bool render_flag) {
  if (!g_native_map_enabled || !chain || !render_world ||
      !g_submit_map_preview || !LiveWorldReadyForNativeRendering() ||
      g_equipment_mutation_in_progress.load(std::memory_order_acquire) ||
      g_equipment_settlement_active.load(std::memory_order_acquire) ||
      g_deferred_ui3d_target.active || g_map_capture.readback_pending)
    return false;
  const uint64_t now = GetTickCount64();
  if (now >= g_native_map_requested_until_ms.load(
                 std::memory_order_acquire) ||
      now - g_map_capture.last_dispatch_ms < 200 ||
      ReadOr<uint8_t>(reinterpret_cast<void*>(
                          g_base + kWorldMapSceneActive), 0) != 0)
    return false;
  g_map_capture.last_dispatch_ms = now;
  const uint64_t setup_started_us = PerfNowUs();

  void* render_scene = ReadOr<void*>(
      reinterpret_cast<void*>(g_base + kWorldRootNodePtr), nullptr);
  void* world_camera = render_scene && RegionAllows(render_scene, 0x130)
      ? ReadOr<void*>(reinterpret_cast<uint8_t*>(render_scene) + 0x128,
                      nullptr)
      : nullptr;
  void* native_world_root = ReadOr<void*>(
      reinterpret_cast<void*>(g_base + kNativeMapWorldRootNodePtr), nullptr);
  const uint16_t child_count = native_world_root &&
          RegionAllows(native_world_root, 0x126)
      ? ReadOr<uint16_t>(reinterpret_cast<uint8_t*>(native_world_root) + 0x124,
                         0)
      : 0;
  void** children = child_count
      ? ReadOr<void**>(reinterpret_cast<uint8_t*>(native_world_root) + 0x118,
                        nullptr)
      : nullptr;
  void* map_root = children && RegionAllows(children, sizeof(void*))
      ? ReadOr<void*>(children, nullptr)
      : nullptr;
  if (!render_scene || !world_camera || !native_world_root || !map_root ||
      !RegionAllows(world_camera, 0x188) ||
      !RegionAllows(map_root, 0x110)) {
    LogNativeMapFailure(now, "world-scene-unavailable", E_POINTER,
                        render_scene, world_camera);
    return false;
  }
  auto object_state_hash = [](const void* object, size_t offset,
                              size_t size) {
    uint64_t hash = 1469598103934665603ull;
    if (!object || !size ||
        !RegionAllows(reinterpret_cast<const uint8_t*>(object) + offset,
                      size))
      return uint64_t{0};
    const auto* bytes = reinterpret_cast<const uint8_t*>(object) + offset;
    for (size_t i = 0; i < size; ++i) {
      hash ^= bytes[i];
      hash *= 1099511628211ull;
    }
    return hash;
  };
  const uint64_t gameplay_camera_hash_before =
      object_state_hash(world_camera, 0x48, 0x188 - 0x48);
  // CameraNode is the live parent whose transform normally drives the
  // gameplay NiCamera. Hash its complete inherited NiAVObject state as well:
  // proving the child camera stayed unchanged is not enough if a map path
  // accidentally moved its live parent.
  const uint64_t gameplay_camera_root_hash_before =
      object_state_hash(map_root, 0x48, 0x110 - 0x48);
  void* const shader_camera_before = ReadOr<void*>(
      reinterpret_cast<void*>(g_base + kShaderSceneCamera), nullptr);

  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* context = nullptr;
  if (FAILED(chain->GetDevice(__uuidof(ID3D11Device),
                              reinterpret_cast<void**>(&device))) ||
      !device) {
    LogNativeMapFailure(now, "swapchain-device", E_FAIL, render_scene,
                        world_camera);
    return false;
  }
  device->GetImmediateContext(&context);
  DXGI_SWAP_CHAIN_DESC swap_desc{};
  const HRESULT description_result = chain->GetDesc(&swap_desc);
  if (FAILED(description_result) || !context) {
    if (context) context->Release();
    device->Release();
    LogNativeMapFailure(now, "swapchain-description", description_result,
                        render_scene, world_camera);
    return false;
  }
  D3D11_TEXTURE2D_DESC source{};
  source.Width = swap_desc.BufferDesc.Width;
  source.Height = swap_desc.BufferDesc.Height;
  source.Format = swap_desc.BufferDesc.Format;
  source.SampleDesc = swap_desc.SampleDesc;
  if (!EnsureNativeMapCaptureResources(chain, device, source)) {
    context->Release();
    device->Release();
    LogNativeMapFailure(now, "private-surface", E_FAIL, render_scene,
                        world_camera);
    return false;
  }

  ScopedNativeMapFramebuffer framebuffer;
  ScopedPrivateWorldMapObjects private_camera;
  ScopedNativeMapCamera map_camera;
  ScopedNativeMapSceneProxy map_scene_proxy;
  ScopedPrivateUi3DCamera ui3d_camera;
  ScopedUi3DSceneRootLease ui3d_scene_root;
  bool rendered = false;
  void* ui3d_manager = nullptr;
  int32_t ui3d_scheme = -1;
  g_map_capture.projected_marker_count = 0;
  // Prove private output ownership before touching Skyrim's map LOD/visibility
  // transaction.  A routing failure can therefore never flash the gameplay
  // scene while the bottom page remains on Preparing.
  if (!framebuffer.Acquire(context)) {
    LogNativeMapFailure(now, "private-render-target-routing", E_FAIL,
                        render_scene, world_camera);
  } else if (!private_camera.Acquire(render_scene, map_root, world_camera)) {
    LogNativeMapFailure(now, "private-camera-construction", E_FAIL,
                        render_scene, world_camera);
  } else if (!map_camera.Acquire(private_camera.root())) {
    LogNativeMapFailure(now, "map-camera-initialization", E_FAIL, render_scene,
                        world_camera);
  } else if (!private_camera.ValidateNativeMapCameraUpdate()) {
    LogNativeMapFailure(now, "map-camera-matrix-rebuild", E_FAIL,
                        render_scene, world_camera);
  } else if (!map_scene_proxy.Acquire(render_scene)) {
    LogNativeMapFailure(now, "map-scene-proxy", E_FAIL, render_scene,
                        world_camera);
  } else {
    ui3d_manager = ReadOr<void*>(
        reinterpret_cast<void*>(g_base + kUI3DSceneManagerPtr), nullptr);
    ui3d_scheme = ui3d_manager && RegionAllows(ui3d_manager, 0x118)
        ? ReadOr<int32_t>(reinterpret_cast<uint8_t*>(ui3d_manager) + 0x90, -1)
        : -1;
    if (!ui3d_manager || ui3d_scheme <= 0 || ui3d_scheme >= 8 ||
        !ui3d_camera.Acquire(ui3d_manager, private_camera.camera()) ||
        !ui3d_scene_root.Acquire(ui3d_manager, ui3d_scheme,
                                 map_scene_proxy.root())) {
      LogNativeMapFailure(now, "isolated-ui3d-scene", E_FAIL, render_scene,
                          world_camera);
    } else {
    const uint32_t diagnostic =
        g_native_map_render_diagnostic_frames.load(std::memory_order_acquire);
    if (diagnostic < 3) {
      void* camera = private_camera.camera();
      PluginLog(false,
                "[SKYRIM-MAP-CAMERA] CREATED private=%p gameplay=%p "
                "world_position=(%.1f,%.1f,%.1f) "
                "frustum=(l=%.4f r=%.4f t=%.4f b=%.4f n=%.2f f=%.1f "
                "ortho=%u) view=(offset=%.1f,%.1f,%.1f zoom=%.5f) "
                "scene_camera_untouched=%u",
                camera, world_camera,
                ReadOr<float>(reinterpret_cast<uint8_t*>(camera) + 0xA0,
                              0.0f),
                ReadOr<float>(reinterpret_cast<uint8_t*>(camera) + 0xA4,
                              0.0f),
                ReadOr<float>(reinterpret_cast<uint8_t*>(camera) + 0xA8,
                              0.0f),
                ReadOr<float>(reinterpret_cast<uint8_t*>(camera) + 0x150,
                              0.0f),
                ReadOr<float>(reinterpret_cast<uint8_t*>(camera) + 0x154,
                              0.0f),
                ReadOr<float>(reinterpret_cast<uint8_t*>(camera) + 0x158,
                              0.0f),
                ReadOr<float>(reinterpret_cast<uint8_t*>(camera) + 0x15C,
                              0.0f),
                ReadOr<float>(reinterpret_cast<uint8_t*>(camera) + 0x160,
                              0.0f),
                ReadOr<float>(reinterpret_cast<uint8_t*>(camera) + 0x164,
                              0.0f),
                ReadOr<uint8_t>(reinterpret_cast<uint8_t*>(camera) + 0x168,
                                0),
                g_native_map_view_offset_x.load(std::memory_order_acquire),
                g_native_map_view_offset_y.load(std::memory_order_acquire),
                g_native_map_view_offset_z.load(std::memory_order_acquire),
                g_native_map_view_zoom.load(std::memory_order_acquire),
                 ReadOr<void*>(reinterpret_cast<uint8_t*>(render_scene) + 0x128,
                               nullptr) == world_camera ? 1u : 0u);
      PluginLog(false,
                "[SKYRIM-MAP-SCENE] proxy=%p source_parent=%p "
                "children=%u ui3d_manager=%p scheme=%d full_world_pass=0",
                map_scene_proxy.root(), map_scene_proxy.source_parent(),
                static_cast<unsigned>(map_scene_proxy.child_count()),
                ui3d_manager, ui3d_scheme);
    }
    g_map_capture.projected_marker_count = ProjectNativeMapMarkers(
        private_camera.camera(), g_map_capture.projected_markers,
        dis::kMaxMapMarkers);
    rendered = true;
    }
  }
  if (rendered) {
    void* const isolated_root = private_camera.root();
    void* const isolated_camera = private_camera.camera();
    g_map_capture.last_setup_us = PerfNowUs() - setup_started_us;
    const uint32_t diagnostic_before =
        g_native_map_render_diagnostic_frames.load(std::memory_order_acquire);
    if (diagnostic_before < 3) {
      LogGpuOutputBinding("MAP_RENDER before", context,
                          g_map_capture.render_view,
                          g_map_capture.render_texture,
                          g_map_capture.width, g_map_capture.height);
    }
    // Draw through Skyrim's scene culling/BSShaderAccumulator boundary used by
    // UI3D. Do not recursively invoke Main_RenderWorld here: that function
    // consumes gameplay camera values published by its outer caller and shares
    // Skyrim's G-buffer, shadow, lighting and postprocess targets. The recursive
    // implementation therefore composited the exact gameplay view into this
    // target and made the following top-screen frame shake.
    const uint64_t render_started_us = PerfNowUs();
    const float clear[4] = {0.015f, 0.020f, 0.025f, 1.0f};
    context->ClearRenderTargetView(g_map_capture.render_view, clear);
    context->ClearDepthStencilView(g_map_capture.depth_view,
                                   D3D11_CLEAR_DEPTH, 1.0f, 0);
    if (g_map_capture.pipeline_query)
      context->Begin(g_map_capture.pipeline_query);
    if (g_map_capture.occlusion_query)
      context->Begin(g_map_capture.occlusion_query);
    using Ui3DRenderFn = void(*)(void*, int32_t, void*, bool);
    auto ui3d_render = reinterpret_cast<Ui3DRenderFn>(g_base + kUI3DRender);
    bool dispatched = RegionAllows(reinterpret_cast<void*>(ui3d_render), 1,
                                   true);
    if (dispatched) {
      ScopedUi3DRenderPermission permission;
      dispatched = static_cast<bool>(permission);
      if (dispatched) {
        // The hooks also preserve the private target across Skyrim's two
        // accumulator passes. Install them, but never enable the per-node
        // UI3D trace for the world proxy: one frame contains thousands of
        // nodes and the old diagnostic path stalled the render thread for
        // roughly one second while writing tens of megabytes of logs.
        InstallUi3DTraceHooks(ui3d_manager);
        ScopedPrivateUi3DRender private_render(
            DeferredUi3DTargetOwner::kMap, g_map_capture.width,
            g_map_capture.height, g_map_capture.render_view,
            g_map_capture.depth_view);
        if (diagnostic_before < 3) {
          LogUi3DDirectChildren("[SKYRIM-MAP-PROXY-CHILD]",
                                map_scene_proxy.root());
        }
        ui3d_render(ui3d_manager, ui3d_scheme, ui3d_camera.camera(), true);
      }
    }
    if (g_map_capture.occlusion_query)
      context->End(g_map_capture.occlusion_query);
    if (g_map_capture.pipeline_query)
      context->End(g_map_capture.pipeline_query);
    g_map_capture.last_render_us = PerfNowUs() - render_started_us;
    if (diagnostic_before < 3) {
      LogGpuOutputBinding("MAP_RENDER after", context,
                          g_map_capture.render_view,
                          g_map_capture.render_texture,
                          g_map_capture.width, g_map_capture.height);
    }
    if (!dispatched) {
      rendered = false;
      LogNativeMapFailure(now, "isolated-ui3d-dispatch", E_FAIL,
                          render_scene, world_camera);
    }
    // Restore every private lease before map scene teardown and before the
    // normal gameplay pass. No gameplay NiNode/NiCamera transform was ever
    // modified by this transaction.
    const size_t color_aliases = framebuffer.color_alias_count();
    const size_t depth_aliases = framebuffer.depth_alias_count();
    ui3d_scene_root.Restore();
    ui3d_camera.Restore();
    framebuffer.Restore();
    map_camera.Destroy();
    private_camera.Restore();
    const uint64_t gameplay_camera_hash_after =
        object_state_hash(world_camera, 0x48, 0x188 - 0x48);
    const uint64_t gameplay_camera_root_hash_after =
        object_state_hash(map_root, 0x48, 0x110 - 0x48);
    void* const shader_camera_after = ReadOr<void*>(
        reinterpret_cast<void*>(g_base + kShaderSceneCamera), nullptr);
    const bool gameplay_camera_unchanged =
        gameplay_camera_hash_before == gameplay_camera_hash_after;
    const bool gameplay_camera_root_unchanged =
        gameplay_camera_root_hash_before == gameplay_camera_root_hash_after;
    if (!gameplay_camera_unchanged || !gameplay_camera_root_unchanged) {
      rendered = false;
      LogNativeMapFailure(now, "live-gameplay-camera-mutated", E_FAIL,
                          render_scene, world_camera);
      g_native_render_reset_revision.fetch_add(1,
                                                std::memory_order_acq_rel);
    }
    g_map_capture.readback_pending = rendered;
    // Issue the copy and event query at Present just like the proven item
    // preview path. This orders readback after UI3D's final accumulator drain
    // without extending any camera, scene-root, or framebuffer lease.
    g_map_capture.readback_copy_issued = false;
    g_map_capture.readback_world_epoch =
        g_world_epoch.load(std::memory_order_acquire);
    g_map_capture.readback_world_id =
        g_native_map_world_id.load(std::memory_order_acquire);
    g_map_capture.readback_request_id =
        g_native_map_active_request.load(std::memory_order_acquire);
    g_map_capture.readback_control_revision =
        g_native_map_applied_control_revision.load(
            std::memory_order_acquire);
    g_map_capture.readback_dispatched_ms = now;
    g_map_capture.readback_dispatched_us = PerfNowUs();
    const uint32_t diagnostic =
        g_native_map_render_diagnostic_frames.fetch_add(
            1, std::memory_order_acq_rel);
    if (diagnostic < 3) {
      PluginLog(false,
                "[GPU] [MAP_RENDER] native world pass complete root=%p "
                "map_root=%p camera=%p center=(%.0f,%.0f,%.0f) "
                "capture_source=%p source=%ux%u native_custom=1 "
                "gameplay_custom=%u private_copy=1 gameplay_copy=0 "
                "full_world_pass=0 ui3d_scheme=%d proxy_children=%u "
                "color_aliases=%llu depth_aliases=%llu "
                "gameplay_camera_unchanged=%u hash=%016llX/%016llX "
                "gameplay_root_unchanged=%u root_hash=%016llX/%016llX "
                "scene_camera_untouched=%u shader_camera_restored=%u "
                "shader=%p/%p",
                render_scene, isolated_root, isolated_camera,
                g_native_map_center_x.load(std::memory_order_acquire),
                g_native_map_center_y.load(std::memory_order_acquire),
                g_native_map_center_z.load(std::memory_order_acquire),
                g_map_capture.render_texture, g_map_capture.width,
                g_map_capture.height,
                render_flag ? 1u : 0u,
                ui3d_scheme,
                static_cast<unsigned>(map_scene_proxy.child_count()),
                static_cast<unsigned long long>(color_aliases),
                static_cast<unsigned long long>(depth_aliases),
                gameplay_camera_unchanged ? 1u : 0u,
                static_cast<unsigned long long>(gameplay_camera_hash_before),
                static_cast<unsigned long long>(gameplay_camera_hash_after),
                gameplay_camera_root_unchanged ? 1u : 0u,
                static_cast<unsigned long long>(
                    gameplay_camera_root_hash_before),
                static_cast<unsigned long long>(
                    gameplay_camera_root_hash_after),
                ReadOr<void*>(reinterpret_cast<uint8_t*>(render_scene) + 0x128,
                              nullptr) == world_camera ? 1u : 0u,
                shader_camera_before == shader_camera_after ? 1u : 0u,
                shader_camera_before, shader_camera_after);
    }
  }
  context->Release();
  device->Release();
  return rendered;
}

[[maybe_unused]] void ServiceNativeMapPresentReadback(IDXGISwapChain* chain) {
  if (!g_map_capture.readback_pending) return;
  const uint64_t now = GetTickCount64();
  const uint64_t age = g_map_capture.readback_dispatched_ms
      ? now - g_map_capture.readback_dispatched_ms
      : 0;
  const bool timed_out = age >= kNativeReadbackTimeoutMs;
  auto clear_readback = [&]() {
    g_map_capture.readback_pending = false;
    g_map_capture.readback_copy_issued = false;
    g_map_capture.readback_world_epoch = 0;
    g_map_capture.readback_world_id = 0;
    g_map_capture.readback_request_id = 0;
    g_map_capture.readback_control_revision = 0;
    g_map_capture.readback_dispatched_ms = 0;
    g_map_capture.readback_dispatched_us = 0;
  };
  if (!g_map_capture.device || !g_map_capture.render_texture ||
      !g_map_capture.staging || !g_map_capture.readback_query) {
    LogNativeMapFailure(now, "readback-resources", E_POINTER);
    clear_readback();
    return;
  }
  ID3D11Device* chain_device = nullptr;
  const HRESULT device_result = chain
      ? chain->GetDevice(__uuidof(ID3D11Device),
                         reinterpret_cast<void**>(&chain_device))
      : E_POINTER;
  const bool device_changed = FAILED(device_result) || !chain_device ||
                              chain_device != g_map_capture.device;
  if (chain_device) chain_device->Release();
  ID3D11DeviceContext* context = nullptr;
  g_map_capture.device->GetImmediateContext(&context);
  if (!context) {
    LogNativeMapFailure(now, "readback-context", E_POINTER);
    clear_readback();
    g_native_render_reset_revision.fetch_add(1,
                                              std::memory_order_acq_rel);
    return;
  }
  if (!g_map_capture.readback_copy_issued) {
    context->CopyResource(g_map_capture.staging,
                          g_map_capture.render_texture);
    context->End(g_map_capture.readback_query);
    g_map_capture.readback_copy_issued = true;
    context->Release();
    return;
  }
  BOOL complete = FALSE;
  const HRESULT query_result = context->GetData(
      g_map_capture.readback_query, &complete, sizeof(complete),
      timed_out ? 0 : D3D11_ASYNC_GETDATA_DONOTFLUSH);
  if (query_result == S_FALSE || (query_result == S_OK && !complete)) {
    if (timed_out) context->Flush();
    if (age >= kNativeReadbackAbortMs) {
      PluginLog(false,
                "map-native: GPU readback aborted after %llums; renderer "
                "resources will be recreated",
                static_cast<unsigned long long>(age));
      context->Release();
      LogNativeMapFailure(now, "readback-timeout", query_result);
      clear_readback();
      g_native_render_reset_revision.fetch_add(1,
                                                std::memory_order_acq_rel);
      return;
    }
    context->Release();
    return;
  }
  const uint64_t epoch = g_world_epoch.load(std::memory_order_acquire);
  const bool same_world = !device_changed && LiveWorldReadyForNativeRendering() &&
                          g_map_capture.readback_world_epoch == epoch &&
                          g_map_capture.readback_world_id ==
                              g_native_map_world_id.load(
                                  std::memory_order_acquire);
  if (!same_world || FAILED(query_result)) {
    context->Release();
    ++g_map_capture.stale_frames;
    LogNativeMapFailure(now,
                        device_changed ? "readback-device-changed"
                                       : (!same_world ? "readback-stale-world"
                                                      : "readback-query"),
                        query_result);
    clear_readback();
    if (device_changed)
      g_native_render_reset_revision.fetch_add(1,
                                                std::memory_order_acq_rel);
    return;
  }
  D3D11_QUERY_DATA_PIPELINE_STATISTICS pipeline_statistics = {};
  UINT64 visible_samples = 0;
  const HRESULT pipeline_result = g_map_capture.pipeline_query
      ? context->GetData(g_map_capture.pipeline_query, &pipeline_statistics,
                         sizeof(pipeline_statistics), 0)
      : E_NOINTERFACE;
  const HRESULT occlusion_result = g_map_capture.occlusion_query
      ? context->GetData(g_map_capture.occlusion_query, &visible_samples,
                         sizeof(visible_samples), 0)
      : E_NOINTERFACE;
  PluginLog(false,
            "[SKYRIM-MAP-RENDER] submission pipeline_hr=%08lX "
            "occlusion_hr=%08lX ia_vertices=%llu ia_primitives=%llu "
            "vs=%llu ps=%llu clipped_primitives=%llu visible_samples=%llu",
            static_cast<unsigned long>(pipeline_result),
            static_cast<unsigned long>(occlusion_result),
            static_cast<unsigned long long>(pipeline_statistics.IAVertices),
            static_cast<unsigned long long>(pipeline_statistics.IAPrimitives),
            static_cast<unsigned long long>(pipeline_statistics.VSInvocations),
            static_cast<unsigned long long>(pipeline_statistics.PSInvocations),
            static_cast<unsigned long long>(pipeline_statistics.CPrimitives),
            static_cast<unsigned long long>(visible_samples));
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  const HRESULT map_result = context->Map(
      g_map_capture.staging, 0, D3D11_MAP_READ, 0, &mapped);
  uint8_t minimum_rgb = 255;
  uint8_t maximum_rgb = 0;
  uint64_t sampled_pixel_hash = 0x4D41504652414D45ull;  // "MAPFRAME"
  const bool mapped_resource = SUCCEEDED(map_result);
  bool valid_pixels = mapped_resource && mapped.pData &&
      g_map_capture.capture_width != 0 &&
      g_map_capture.capture_height != 0 &&
      mapped.RowPitch >= g_map_capture.capture_width * 4;
  if (valid_pixels) {
    const bool bgra = g_map_capture.format == DXGI_FORMAT_B8G8R8A8_UNORM ||
                      g_map_capture.format ==
                          DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    for (uint32_t y = 0; y < g_map_capture.height; ++y) {
      const uint32_t source_y = std::min(
          g_map_capture.capture_height - 1,
          static_cast<uint32_t>(
              (static_cast<uint64_t>(y) * 2 + 1) *
              g_map_capture.capture_height /
              (static_cast<uint64_t>(g_map_capture.height) * 2)));
      const auto* input_row = static_cast<const uint8_t*>(mapped.pData) +
          static_cast<size_t>(source_y) * mapped.RowPitch;
      auto* output_row = g_map_frame_rgba +
          static_cast<size_t>(y) * g_map_capture.width * 4;
      for (uint32_t x = 0; x < g_map_capture.width; ++x) {
        const uint32_t source_x = std::min(
            g_map_capture.capture_width - 1,
            static_cast<uint32_t>(
                (static_cast<uint64_t>(x) * 2 + 1) *
                g_map_capture.capture_width /
                (static_cast<uint64_t>(g_map_capture.width) * 2)));
        const uint8_t* input = input_row + source_x * 4;
        uint8_t* output = output_row + x * 4;
        output[0] = bgra ? input[2] : input[0];
        output[1] = input[1];
        output[2] = bgra ? input[0] : input[2];
        output[3] = 255;
        if ((x & 15u) == 0 && (y & 15u) == 0) {
          minimum_rgb = std::min(
              {minimum_rgb, output[0], output[1], output[2]});
          maximum_rgb = std::max(
              {maximum_rgb, output[0], output[1], output[2]});
          const uint32_t packed = static_cast<uint32_t>(output[0]) |
              (static_cast<uint32_t>(output[1]) << 8) |
              (static_cast<uint32_t>(output[2]) << 16) |
              (static_cast<uint32_t>(output[3]) << 24);
          sampled_pixel_hash ^= packed;
          sampled_pixel_hash *= 1099511628211ull;
        }
      }
    }
  }
  if (mapped_resource) context->Unmap(g_map_capture.staging, 0);
  context->Release();
  const uint64_t request = g_map_capture.readback_request_id;
  const uint64_t captured_control_revision =
      g_map_capture.readback_control_revision;
  const uint32_t captured_world_id = g_map_capture.readback_world_id;
  const uint32_t captured_marker_count = std::min(
      g_map_capture.projected_marker_count, dis::kMaxMapMarkers);
  const uint64_t dispatched_us = g_map_capture.readback_dispatched_us;
  clear_readback();
  const bool pixels_have_geometry =
      valid_pixels && maximum_rgb > minimum_rgb + 6;
  const bool pipeline_has_geometry =
      pipeline_result != S_OK ||
      (pipeline_statistics.IAPrimitives != 0 &&
       pipeline_statistics.VSInvocations != 0 &&
       pipeline_statistics.PSInvocations != 0);
  const bool occlusion_has_geometry =
      occlusion_result != S_OK || visible_samples != 0;
  // A private frame is publishable only when the dedicated texture changed
  // away from its uniform clear and, when supported by the driver, Skyrim's
  // pipeline and occlusion queries prove real world geometry was submitted.
  // No arbitrary non-flat gameplay image can satisfy this path because the
  // swapchain back buffer is never copied into the staging resource.
  const bool has_scene = pixels_have_geometry && pipeline_has_geometry &&
                         occlusion_has_geometry;
  if (has_scene) {
    const uint64_t previous_pixel_hash = g_map_capture.last_pixel_hash;
    g_map_capture.last_pixel_hash = sampled_pixel_hash;
    const uint64_t local_revision = ++g_map_capture.source_revision;
    const uint64_t revision = ((epoch & 0xFFFFFFFFull) << 32) |
        (local_revision & 0xFFFFFFFFull);
    g_submit_map_preview(g_map_capture.width, g_map_capture.height,
                         g_map_frame_rgba, g_map_capture.width * 4,
                         captured_world_id, revision,
                         g_map_capture.projected_markers,
                         captured_marker_count);
    // A static request ends after one publication. If a drag/pinch arrived
    // after this frame's camera was installed, start exactly one follow-up
    // transaction for the latest coalesced revision; no gesture packet is
    // lost and the native world pass never runs continuously while idle.
    uint64_t latest_control_revision =
        g_native_map_control_revision.load(std::memory_order_acquire);
    bool pending_control = NativeMapInputPending();
    const uint64_t request_started =
        g_native_map_request_started_ms.load(std::memory_order_acquire);
    bool followup = false;
    uint64_t followup_request = 0;
    // Close the active deadline before the final revision check. A command
    // that raced the first load either saw the old active deadline (and is
    // caught by this reload) or sees zero and starts its own request. This
    // removes the last window in which a coalesced pointer-up could be lost.
    if (g_native_map_active_request.load(std::memory_order_acquire) == request &&
        latest_control_revision <= captured_control_revision &&
        !pending_control) {
      g_native_map_requested_until_ms.store(0, std::memory_order_release);
      latest_control_revision =
          g_native_map_control_revision.load(std::memory_order_acquire);
      pending_control = NativeMapInputPending();
    }
    if (g_native_map_active_request.load(std::memory_order_acquire) == request &&
        (latest_control_revision > captured_control_revision ||
         pending_control)) {
      followup_request = g_native_map_request_serial.fetch_add(
          1, std::memory_order_acq_rel) + 1;
      g_native_map_active_request.store(followup_request,
                                        std::memory_order_release);
      g_native_map_request_started_ms.store(now, std::memory_order_release);
      g_native_map_lock_misses.store(0, std::memory_order_release);
      g_native_map_request_failure_base.store(
          g_native_map_failure_count.load(std::memory_order_acquire),
          std::memory_order_release);
      g_native_map_last_failure_reason.store("not-dispatched",
                                             std::memory_order_release);
      g_native_map_last_failure_hr.store(S_OK, std::memory_order_release);
      g_native_map_requested_until_ms.store(
          now + kNativeCaptureRetryWindowMs, std::memory_order_release);
      followup = true;
    }
    uint64_t prior_terminal = g_native_map_terminal_request.load(
        std::memory_order_acquire);
    while (prior_terminal < request &&
           !g_native_map_terminal_request.compare_exchange_weak(
               prior_terminal, request, std::memory_order_acq_rel,
               std::memory_order_acquire)) {}
    g_map_capture.active_world_epoch = epoch;
    ++g_map_capture.captured_frames;
    PluginLog(false,
              "[SKYRIM-MAP] request=%llu SUCCESS total_ms=%llu "
              "setup_us=%llu render_us=%llu readback_us=%llu rgb=%u..%u "
              "revision=%llu markers=%u control_revision=%llu "
              "latest_control=%llu pixel_hash=%016llX changed=%u "
              "pending_control=%u followup=%u followup_request=%llu",
              static_cast<unsigned long long>(request),
              static_cast<unsigned long long>(
                  request_started ? now - request_started : 0),
              static_cast<unsigned long long>(g_map_capture.last_setup_us),
              static_cast<unsigned long long>(g_map_capture.last_render_us),
              static_cast<unsigned long long>(
                  dispatched_us ? PerfNowUs() - dispatched_us : 0),
              minimum_rgb, maximum_rgb,
              static_cast<unsigned long long>(revision),
              captured_marker_count,
              static_cast<unsigned long long>(captured_control_revision),
              static_cast<unsigned long long>(latest_control_revision),
              static_cast<unsigned long long>(sampled_pixel_hash),
              previous_pixel_hash && previous_pixel_hash != sampled_pixel_hash
                  ? 1u
                  : 0u,
              pending_control ? 1u : 0u,
              followup ? 1u : 0u,
              static_cast<unsigned long long>(followup_request));
  } else {
    ++g_map_capture.transition_frames;
    const char* reason = !valid_pixels
        ? "gpu-readback"
        : !pixels_have_geometry
            ? "private-target-clear-only"
            : !pipeline_has_geometry
                ? "no-map-draw-calls"
                : "empty-visible-set";
    LogNativeMapFailure(now, reason, map_result);
  }
  if (now - g_map_capture.last_log_ms >= 10000) {
    PluginLog(false,
              "map-native: private frames=%llu failed=%llu stale=%llu "
              "empty=%llu rgb=%u..%u revision=%llu epoch=%llu",
              static_cast<unsigned long long>(g_map_capture.captured_frames),
              static_cast<unsigned long long>(g_map_capture.failed_frames),
              static_cast<unsigned long long>(g_map_capture.stale_frames),
              static_cast<unsigned long long>(
                  g_map_capture.transition_frames),
              minimum_rgb, maximum_rgb,
              static_cast<unsigned long long>(g_map_capture.source_revision),
              static_cast<unsigned long long>(
                  g_map_capture.active_world_epoch));
    g_map_capture.last_log_ms = now;
  }
}

IDXGISwapChain* CurrentPrimarySwapChain() {
  void* renderer_data = ReadOr<void*>(
      reinterpret_cast<void*>(g_base + kRendererDataPtr), nullptr);
  void* window = ReadOr<void*>(
      reinterpret_cast<void*>(g_base + kCurrentRenderWindowPtr), nullptr);
  if (!window && renderer_data && RegionAllows(renderer_data, 0x98))
    window = reinterpret_cast<uint8_t*>(renderer_data) + 0x48;
  return window
             ? ReadOr<IDXGISwapChain*>(
                   reinterpret_cast<uint8_t*>(window) + 0x18, nullptr)
             : nullptr;
}

bool IsPrimaryCaptureChain(IDXGISwapChain* chain) {
  if (!chain) return false;
  IDXGISwapChain* primary =
      g_primary_capture_chain.load(std::memory_order_acquire);
  if (chain == primary) return true;
  // Renderer resets may replace the swapchain without changing the shared
  // Present vtable. Refresh the identity only when the cached chain does not
  // match, then continue to reject WEMU/other auxiliary swapchains.
  primary = CurrentPrimarySwapChain();
  if (primary)
    g_primary_capture_chain.store(primary, std::memory_order_release);
  return chain == primary;
}

void MainRenderWorldHook(bool render_flag) {
  MainRenderWorldFn original =
      g_main_render_world_original.load(std::memory_order_acquire);
  if (!original) return;
  const uint64_t map_now = GetTickCount64();
  FinalizeExpiredNativeMapRequest(map_now);
  if (!g_inside_native_map_world_render && g_native_map_enabled &&
      g_capture_present_hooked.load(std::memory_order_acquire) &&
      LiveWorldReadyForNativeRendering() &&
      map_now < g_native_map_requested_until_ms.load(
                    std::memory_order_acquire) &&
      !g_equipment_mutation_in_progress.load(std::memory_order_acquire) &&
      !g_equipment_settlement_active.load(std::memory_order_acquire)) {
    if (TryAcquireSRWLockExclusive(&g_native_render_lock)) {
      g_inside_native_map_world_render = true;
      try {
        RenderNativeMapPassBeforeGameplay(CurrentPrimarySwapChain(), original,
                                          render_flag);
      } catch (...) {
        PluginLog(false,
                  "map-native: C++ exception in pre-gameplay map pass; "
                  "request cancelled and renderer reset scheduled");
        LogNativeMapFailure(map_now, "render-exception", E_FAIL);
        g_native_map_requested_until_ms.store(map_now,
                                              std::memory_order_release);
        FinalizeExpiredNativeMapRequest(map_now);
        g_native_render_reset_revision.fetch_add(1,
                                                  std::memory_order_acq_rel);
      }
      g_inside_native_map_world_render = false;
      ReleaseSRWLockExclusive(&g_native_render_lock);
    } else {
      g_native_map_lock_misses.fetch_add(1, std::memory_order_relaxed);
    }
  }
  // This is always the final pass. It restores Skyrim's top-screen gameplay
  // image and re-establishes normal renderer state even after a map capture.
  original(render_flag);
}

void* AllocateExecutableRelayNear(void* callsite, size_t bytes) {
  if (!callsite || bytes == 0) return nullptr;
  SYSTEM_INFO info{};
  GetSystemInfo(&info);
  const uintptr_t site = reinterpret_cast<uintptr_t>(callsite);
  const uintptr_t system_min = reinterpret_cast<uintptr_t>(
      info.lpMinimumApplicationAddress);
  const uintptr_t system_max = reinterpret_cast<uintptr_t>(
      info.lpMaximumApplicationAddress);
  constexpr uintptr_t kRel32Reach = 0x7FFF0000ull;
  const uintptr_t low = site > kRel32Reach
      ? std::max(system_min, site - kRel32Reach)
      : system_min;
  const uintptr_t high = std::min(system_max, site + kRel32Reach);
  const uintptr_t granularity = std::max<uintptr_t>(
      info.dwAllocationGranularity, 0x1000u);
  uintptr_t cursor = low;
  while (cursor < high) {
    MEMORY_BASIC_INFORMATION region{};
    if (!VirtualQuery(reinterpret_cast<void*>(cursor), &region,
                      sizeof(region)))
      break;
    const uintptr_t region_base = reinterpret_cast<uintptr_t>(
        region.BaseAddress);
    const uintptr_t region_end = region_base + region.RegionSize;
    if (region.State == MEM_FREE) {
      const uintptr_t candidate =
          (std::max(region_base, low) + granularity - 1) &
          ~(granularity - 1);
      if (candidate < high && candidate + bytes <= region_end &&
          candidate + bytes <= high) {
        void* relay = VirtualAlloc(reinterpret_cast<void*>(candidate), bytes,
                                   MEM_RESERVE | MEM_COMMIT,
                                   PAGE_READWRITE);
        if (relay) return relay;
      }
    }
    if (region_end <= cursor) break;
    cursor = region_end;
  }
  return nullptr;
}

bool InstallMainRenderWorldHook() {
  if (!g_native_map_enabled) return true;
  if (g_main_render_world_hooked.load(std::memory_order_acquire)) return true;
  auto* callsite = reinterpret_cast<uint8_t*>(
      g_base + kWorldRenderCallsite);
  if (!RegionAllows(callsite, 5, true) || callsite[0] != 0xE8) {
    PluginLog(false,
              "map-native: world-render hook rejected; callsite opcode=%02X",
              RegionAllows(callsite, 1) ? callsite[0] : 0u);
    return false;
  }
  int32_t current_relative = 0;
  std::memcpy(&current_relative, callsite + 1, sizeof(current_relative));
  auto* current_target = callsite + 5 + current_relative;
  auto* expected_target = reinterpret_cast<uint8_t*>(
      g_base + kMainRenderWorld);
  if (current_target != expected_target ||
      !RegionAllows(expected_target, 1, true)) {
    PluginLog(false,
              "map-native: world-render hook rejected; expected=%p actual=%p",
              expected_target, current_target);
    return false;
  }

  constexpr size_t kRelaySize = 12;
  void* relay = AllocateExecutableRelayNear(callsite, kRelaySize);
  if (!relay) {
    PluginLog(false,
              "map-native: world-render hook failed; no rel32 relay region");
    return false;
  }
  uint8_t relay_code[kRelaySize] = {0x48, 0xB8};
  const uintptr_t hook_address = reinterpret_cast<uintptr_t>(
      &MainRenderWorldHook);
  std::memcpy(relay_code + 2, &hook_address, sizeof(hook_address));
  relay_code[10] = 0xFF;
  relay_code[11] = 0xE0;
  std::memcpy(relay, relay_code, sizeof(relay_code));
  DWORD relay_old = 0;
  if (!VirtualProtect(relay, kRelaySize, PAGE_EXECUTE_READ, &relay_old)) {
    VirtualFree(relay, 0, MEM_RELEASE);
    PluginLog(false,
              "map-native: world-render relay protection failed error=%lu",
              static_cast<unsigned long>(GetLastError()));
    return false;
  }
  FlushInstructionCache(GetCurrentProcess(), relay, kRelaySize);

  const int64_t relative64 = reinterpret_cast<uint8_t*>(relay) -
      (callsite + 5);
  if (relative64 < INT32_MIN || relative64 > INT32_MAX) {
    VirtualFree(relay, 0, MEM_RELEASE);
    PluginLog(false,
              "map-native: allocated world-render relay is out of rel32 range");
    return false;
  }
  const int32_t relative = static_cast<int32_t>(relative64);
  uint8_t patch[5] = {0xE8};
  std::memcpy(patch + 1, &relative, sizeof(relative));
  DWORD old_protect = 0;
  if (!VirtualProtect(callsite, sizeof(patch), PAGE_EXECUTE_READWRITE,
                      &old_protect)) {
    VirtualFree(relay, 0, MEM_RELEASE);
    PluginLog(false,
              "map-native: world-render callsite protection failed error=%lu",
              static_cast<unsigned long>(GetLastError()));
    return false;
  }
  g_main_render_world_original.store(
      reinterpret_cast<MainRenderWorldFn>(expected_target),
      std::memory_order_release);
  std::memcpy(callsite, patch, sizeof(patch));
  FlushInstructionCache(GetCurrentProcess(), callsite, sizeof(patch));
  DWORD ignored = 0;
  const BOOL restored = VirtualProtect(callsite, sizeof(patch), old_protect,
                                       &ignored);
  g_main_render_world_relay = relay;
  g_main_render_world_hooked.store(true, std::memory_order_release);
  PluginLog(false,
            "map-native: Main_RenderWorld callsite hooked site=%p "
            "original=%p relay=%p protection_restored=%u",
            callsite, expected_target, relay, restored ? 1u : 0u);
  return true;
}

bool ServiceCharacterDetachForReset(void* manager, uint64_t now) {
  if (!g_character_render.active_clone) {
    g_character_render.reset_detach_attempts = 0;
    g_character_render.reset_detach_next_ms = 0;
    g_character_render.reset_detach_exhausted_logged = false;
    return true;
  }
  constexpr uint32_t kMaximumDetachAttempts = 12;
  if (g_character_render.reset_detach_attempts >=
      kMaximumDetachAttempts) {
    if (!g_character_render.reset_detach_exhausted_logged) {
      PluginLog(false,
                "character-preview: reset detach retries exhausted; clone=%p "
                "retained safely and native rendering remains suspended",
                g_character_render.active_clone);
      g_character_render.reset_detach_exhausted_logged = true;
    }
    return false;
  }
  if (now < g_character_render.reset_detach_next_ms) return false;

  ++g_character_render.reset_detach_attempts;
  if (DetachActiveCharacter(manager)) {
    g_character_render.reset_detach_attempts = 0;
    g_character_render.reset_detach_next_ms = 0;
    g_character_render.reset_detach_exhausted_logged = false;
    return true;
  }
  const uint32_t exponent =
      std::min<uint32_t>(g_character_render.reset_detach_attempts, 4u);
  const uint64_t delay = std::min<uint64_t>(1000, 50ull << exponent);
  g_character_render.reset_detach_next_ms = now + delay;
  return false;
}

bool RemoveExactPrivateOmBindings(ID3D11Device* device) {
  if (!device) return true;
  ID3D11DeviceContext* context = nullptr;
  device->GetImmediateContext(&context);
  if (!context) return false;
  ID3D11RenderTargetView* original_views[
      D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
  ID3D11DepthStencilView* original_depth = nullptr;
  context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
                              original_views, &original_depth);
  ID3D11RenderTargetView* desired_views[
      D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
  for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    desired_views[i] = original_views[i];
  ID3D11DepthStencilView* desired_depth = original_depth;
  bool changed = false;
  for (auto*& view : desired_views) {
    if (view && (view == g_character_render.render_view ||
                 view == g_native_capture.render_view ||
                 view == g_map_capture.render_view)) {
      view = nullptr;
      changed = true;
    }
  }
  if (desired_depth &&
      (desired_depth == g_character_render.depth_view ||
       desired_depth == g_native_capture.depth_view ||
       desired_depth == g_map_capture.depth_view)) {
    desired_depth = nullptr;
    changed = true;
  }
  if (changed) {
    context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
                                desired_views, desired_depth);
    PluginLog(false,
              "[GPU_OWNERSHIP] reset unbound exact private OM components "
              "device=%p",
              device);
  }
  if (original_depth) original_depth->Release();
  for (auto* view : original_views)
    if (view) view->Release();

  ID3D11RenderTargetView* verify_views[
      D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
  ID3D11DepthStencilView* verify_depth = nullptr;
  context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
                              verify_views, &verify_depth);
  bool clean = verify_depth != g_character_render.depth_view &&
               verify_depth != g_native_capture.depth_view &&
               verify_depth != g_map_capture.depth_view;
  for (auto* view : verify_views) {
    clean = clean && view != g_character_render.render_view &&
            view != g_native_capture.render_view &&
            view != g_map_capture.render_view;
    if (view) view->Release();
  }
  if (verify_depth) verify_depth->Release();
  context->Release();
  return clean;
}

bool FramebufferBelongsToCurrentChain(const RenderTargetDataRaw& framebuffer) {
  IDXGISwapChain* chain = CurrentPrimarySwapChain();
  if (!chain || !framebuffer.texture || !framebuffer.render_view) return false;
  ID3D11Device* chain_device = nullptr;
  if (FAILED(chain->GetDevice(__uuidof(ID3D11Device),
                              reinterpret_cast<void**>(&chain_device))) ||
      !chain_device)
    return false;
  auto child_matches = [&](auto* child) {
    if (!child) return true;
    ID3D11Device* child_device = nullptr;
    child->GetDevice(&child_device);
    const bool matches = child_device == chain_device;
    if (child_device) child_device->Release();
    return matches;
  };
  const bool matches = child_matches(framebuffer.texture) &&
      child_matches(framebuffer.texture_copy) &&
      child_matches(framebuffer.render_view) &&
      child_matches(framebuffer.shader_view) &&
      child_matches(framebuffer.shader_copy) &&
      child_matches(framebuffer.unordered_view);
  chain_device->Release();
  return matches;
}

bool ServiceNativeRenderReset() {
  const uint64_t requested =
      g_native_render_reset_revision.load(std::memory_order_acquire);
  if (requested != g_observed_native_render_reset_revision) {
    g_observed_native_render_reset_revision = requested;
    g_native_render_reset_pending = true;
    g_native_render_reset_resources_retired = false;
    g_native_render_reset_timeout_logged = false;
    g_native_render_reset_started_ms = GetTickCount64();
    g_character_render.reset_detach_attempts = 0;
    g_character_render.reset_detach_next_ms = 0;
    g_character_render.reset_detach_exhausted_logged = false;

    PluginLog(false,
              "native-render: reset observed revision=%llu item_stage=%u "
              "pending=%u",
              static_cast<unsigned long long>(requested),
              static_cast<unsigned>(g_native_capture.stage),
              g_native_render_reset_pending ? 1u : 0u);
  }

  if (!g_native_render_reset_pending) return true;
  const uint64_t now = GetTickCount64();
  if (!g_native_render_reset_resources_retired) {
    // Releasing a D3D query/staging texture does not cancel already submitted
    // GPU work. Present services the query before this function; wait until it
    // is genuinely complete instead of clearing ownership flags by hand.
    if (g_deferred_ui3d_target.active ||
        g_character_render.readback_pending ||
        g_character_render.readback_copy_issued ||
        g_native_capture.readback_pending ||
        g_native_capture.readback_copy_issued ||
        g_map_capture.readback_pending ||
        g_map_capture.readback_copy_issued)
      return false;
    // A lost metadata lease can still leave one of our private RTV/DSV
    // objects bound directly on the immediate context. Remove only exact
    // private bindings, preserving every independently changed Skyrim slot,
    // before any owning COM references are released below.
    ID3D11Device* const character_device = g_character_render.device;
    ID3D11Device* const item_device = g_native_capture.resource_device;
    ID3D11Device* const map_device = g_map_capture.device;
    if (character_device &&
        !RemoveExactPrivateOmBindings(character_device)) {
      if (!g_native_render_reset_timeout_logged) {
        PluginLog(false,
                  "[GPU_OWNERSHIP] native reset quarantined: character "
                  "private OM binding could not be retired");
        g_native_render_reset_timeout_logged = true;
      }
      return false;
    }
    if (item_device && item_device != character_device &&
        !RemoveExactPrivateOmBindings(item_device)) {
      if (!g_native_render_reset_timeout_logged) {
        PluginLog(false,
                  "[GPU_OWNERSHIP] native reset quarantined: item private "
                  "OM binding could not be retired");
        g_native_render_reset_timeout_logged = true;
      }
      return false;
    }
    if (map_device && map_device != character_device &&
        map_device != item_device &&
        !RemoveExactPrivateOmBindings(map_device)) {
      if (!g_native_render_reset_timeout_logged) {
        PluginLog(false,
                  "[GPU_OWNERSHIP] native reset quarantined: map private "
                  "OM binding could not be retired");
        g_native_render_reset_timeout_logged = true;
      }
      return false;
    }
    // RendererTargetData stores raw pointers. If an ownership-loss path ever
    // leaves even one private pointer published, releasing the COM resources
    // here would turn that registry slot into a dangling pointer. Quarantine
    // until Skyrim republishes a framebuffer belonging to the live chain.
    auto* renderer = reinterpret_cast<uint8_t*>(g_base + kRendererSingleton);
    auto* framebuffer = reinterpret_cast<RenderTargetDataRaw*>(
        renderer + kRendererFramebufferOffset);
    RenderTargetDataRaw published_framebuffer = {};
    const bool framebuffer_readable =
        RegionAllows(framebuffer, sizeof(*framebuffer)) &&
        Read(framebuffer, &published_framebuffer);
    const auto private_target_still_published =
        [&](ID3D11Texture2D* texture, ID3D11RenderTargetView* render_view,
            ID3D11ShaderResourceView* shader_view) {
          return framebuffer_readable &&
              ((texture &&
                (published_framebuffer.texture == texture ||
                 published_framebuffer.texture_copy == texture)) ||
               (render_view &&
                published_framebuffer.render_view == render_view) ||
               (shader_view &&
                (published_framebuffer.shader_view == shader_view ||
                 published_framebuffer.shader_copy == shader_view)));
        };
    const bool private_target_published =
        private_target_still_published(
            g_character_render.render_texture,
            g_character_render.render_view,
            g_character_render.shader_view) ||
        private_target_still_published(
            g_native_capture.render_texture,
            g_native_capture.render_view,
            g_native_capture.shader_view) ||
        private_target_still_published(
            g_map_capture.render_texture,
            g_map_capture.render_view,
            g_map_capture.shader_view);
    if (private_target_published) {
      if (!g_native_render_reset_timeout_logged) {
        PluginLog(false,
                  "[GPU_OWNERSHIP] native reset quarantined: renderer "
                  "still publishes private framebuffer pointers");
        g_native_render_reset_timeout_logged = true;
      }
      return false;
    }
    const bool render_resources_exist =
        character_device || item_device || map_device;
    if (render_resources_exist &&
        (!framebuffer_readable ||
         !FramebufferBelongsToCurrentChain(published_framebuffer))) {
      if (!g_native_render_reset_timeout_logged) {
        PluginLog(false,
                  "[GPU_OWNERSHIP] native reset quarantined: renderer "
                  "framebuffer is absent or belongs to a stale device");
        g_native_render_reset_timeout_logged = true;
      }
      return false;
    }
    ReleaseCharacterRenderTextures();
    g_character_render.has_good_frame = false;
    g_character_render.cached_world_epoch = 0;
    g_character_render.cached_source_revision = 0;
    g_character_render.cached_appearance_signature = 0;
    g_character_cached_signature.store(0, std::memory_order_release);
    g_character_cached_world_epoch.store(0, std::memory_order_release);
    ReleaseNativeMapCaptureResources();
    g_map_capture.last_dispatch_ms = 0;
    if (g_set_character_ready) g_set_character_ready(0);

    if (g_native_capture.stage == NativeCaptureStage::kIdle &&
        !g_native_capture.attached_model) {
      if (!ResetNativeCapture(false)) return false;
    } else {
      // The paired native shutdown owns cancellation, model detachment, task
      // release, temporary-reference destruction, and UI3D teardown.
      CompleteActiveNativePreviewFailure("native renderer reset");
      g_native_capture.stage = NativeCaptureStage::kUnloading;
    }
    g_native_render_reset_resources_retired = true;
  }
  void* scene_manager = ReadOr<void*>(
      reinterpret_cast<void*>(g_base + kUI3DSceneManagerPtr), nullptr);
  if (!ServiceCharacterDetachForReset(scene_manager, now)) return false;
  if (g_native_capture.stage == NativeCaptureStage::kIdle &&
      !g_native_capture.attached_model) {
    ReleaseCharacterUi3DScheme(scene_manager);
    if (g_character_render.scheme_session_owned) return false;
    g_native_render_reset_pending = false;
    return true;
  }
  void* inventory_manager = ReadOr<void*>(
      reinterpret_cast<void*>(g_base + kInventory3DManagerPtr), nullptr);
  if (!inventory_manager || !RegionAllows(inventory_manager, 0x160)) {
    // Manager disappearance is not proof that an asynchronous task released
    // its pointer to our embedded InventoryEntryData. Quarantine the static
    // storage until a readable neutral manager returns (or process exit).
    if (now - g_native_render_reset_started_ms >= 2000) {
      NoteNativeItemUnloadBlocked("renderer reset manager unavailable",
                                  inventory_manager, now);
      if (!ResetNativeCapture(false)) return false;
      ReleaseCharacterUi3DScheme(scene_manager);
      if (g_character_render.scheme_session_owned) return false;
      g_native_render_reset_pending = false;
      return true;
    }
    return false;
  }

  if (g_native_capture.manager_lifecycle_owned &&
      !g_native_capture.unload_called &&
      !NativeInventoryLifecycleMatches(inventory_manager)) {
    g_native_capture.manager_ownership_lost = true;
    // Another native menu now owns shutdown. Mark our shutdown phase as
    // handed off so reset waits for its authoritative neutral state instead
    // of repeatedly calling a no-op local lifecycle release.
    g_native_capture.unload_called = true;
    ClearNativeInventoryLifecycleToken();
    g_native_capture.attached_model = nullptr;
    g_native_capture.attached_scheme = -1;
    if (!ResetNativeCapture(false)) return false;
    ReleaseCharacterUi3DScheme(scene_manager);
    if (g_character_render.scheme_session_owned) return false;
    g_native_render_reset_pending = false;
    PluginLog(false,
              "native-render: reset retired item capture after manager "
              "ownership changed revision=%llu",
              static_cast<unsigned long long>(requested));
    return true;
  }

  if (!g_native_capture.unload_called) {
    if (g_native_capture.manager_lifecycle_owned) {
      if (!ReleaseNativeInventoryLifecycle(inventory_manager)) {
        NoteNativeItemUnloadBlocked("renderer reset shutdown incomplete",
                                    inventory_manager, now);
        return false;
      }
      g_native_capture.attached_model = nullptr;
      g_native_capture.attached_scheme = -1;
      return false;
    }
    if (g_native_capture.entry_submitted &&
        !NativeInventoryManagerNeutral(inventory_manager)) {
      NoteNativeItemUnloadBlocked("renderer reset shutdown owner unavailable",
                                  inventory_manager, now);
      return false;
    }
    g_native_capture.unload_called = true;
  }

  LoadedInventoryModelRaw* models = nullptr;
  uint32_t model_count = 0;
  if (!InventoryLoadInFlight(inventory_manager) &&
      InventoryModels(inventory_manager, &models, &model_count) &&
      model_count == 0 &&
      ReadOr<int32_t>(reinterpret_cast<uint8_t*>(inventory_manager) + 0x34,
                      -1) == 0 &&
      !ReadOr<void*>(reinterpret_cast<uint8_t*>(inventory_manager) + 0x38,
                     nullptr)) {
    if (!ResetNativeCapture(false)) return false;
    ReleaseCharacterUi3DScheme(scene_manager);
    if (g_character_render.scheme_session_owned) return false;
    g_native_render_reset_pending = false;
    PluginLog(false, "native-render: reset cleanup complete revision=%llu",
              static_cast<unsigned long long>(requested));
    return true;
  }
  if (now - g_native_render_reset_started_ms >= 10000) {
    if (!g_native_render_reset_timeout_logged) {
      PluginLog(false,
                "native-render: reset cleanup timed out; engine-owned item "
                "state retained in quarantine until manager is neutral");
      g_native_render_reset_timeout_logged = true;
    }
    NoteNativeItemUnloadBlocked("renderer reset manager not neutral",
                                inventory_manager, now);
    // ResetNativeCapture is defensive and will only succeed if the manager
    // became neutral between the checks above. Never time-force a release.
    if (!ResetNativeCapture(false)) return false;
    ReleaseCharacterUi3DScheme(scene_manager);
    if (g_character_render.scheme_session_owned) return false;
    g_native_render_reset_pending = false;
    return true;
  }
  return false;
}

void ServiceInventoryPresentReadback(IDXGISwapChain* chain) {
  if (!g_native_capture.readback_pending) return;
  const uint64_t dispatched_ms = g_native_capture.readback_dispatched_ms;
  auto cancel_readback = [&]() {
    g_native_capture.readback_pending = false;
    g_native_capture.readback_copy_issued = false;
    g_native_capture.readback_dispatched_ms = 0;
    g_native_capture.readback_ready = false;
    g_native_capture.readback_success = false;
  };
  auto finish_failed_readback = [&]() {
    g_native_capture.readback_pending = false;
    g_native_capture.readback_copy_issued = false;
    g_native_capture.readback_dispatched_ms = 0;
    g_native_capture.content_samples = 0;
    g_native_capture.background_samples = 0;
    g_native_capture.depth_samples = 0;
    g_native_capture.readback_success = false;
    g_native_capture.readback_ready = true;
    g_native_capture.engine_render_result = 1;
  };
  if (!g_native_capture.resource_device || !g_native_capture.readback_query ||
      (!g_native_capture.readback_copy_issued &&
       (!g_native_capture.render_texture || !g_native_capture.staging ||
        !g_native_capture.depth_texture ||
        !g_native_capture.depth_staging))) {
    cancel_readback();
    return;
  }
  const uint64_t readback_age =
      dispatched_ms ? GetTickCount64() - dispatched_ms : 0;
  const bool timed_out = dispatched_ms &&
                         readback_age >= kNativeReadbackTimeoutMs;
  const bool same_request =
      g_native_capture.stage == NativeCaptureStage::kCapturing &&
      g_native_capture.request.world_epoch ==
          g_world_epoch.load(std::memory_order_acquire);

  ID3D11Device* chain_device = nullptr;
  const HRESULT chain_device_result =
      chain ? chain->GetDevice(__uuidof(ID3D11Device),
                               reinterpret_cast<void**>(&chain_device))
            : E_POINTER;
  const bool chain_device_available =
      SUCCEEDED(chain_device_result) && chain_device;
  const bool device_changed =
      !chain_device_available ||
      chain_device != g_native_capture.resource_device;
  if (device_changed) {
    PluginLog(false,
              "[GPU] [ITEM_RENDER] device changed before Present readback "
              "cached=%p current=%p chain_hr=%08lX; draining "
              "cached-device query",
              g_native_capture.resource_device, chain_device,
              static_cast<unsigned long>(chain_device_result));
    g_native_render_reset_revision.fetch_add(1, std::memory_order_acq_rel);
  }
  if (chain_device) chain_device->Release();
  ID3D11DeviceContext* context = nullptr;
  g_native_capture.resource_device->GetImmediateContext(&context);
  if (!context) {
    PluginLog(false,
              "[GPU] [ITEM_RENDER] cached device has no immediate context "
              "removed=%08lX",
              static_cast<unsigned long>(
                  g_native_capture.resource_device->GetDeviceRemovedReason()));
    g_native_render_reset_revision.fetch_add(1, std::memory_order_acq_rel);
    finish_failed_readback();
    return;
  }

  // Do not CopyResource and immediately Map on the primary Present hook. That
  // forces Wine/VKD3D to drain the GPU and was costing a full frame on Thor.
  // Queue the copy now and poll it non-blockingly on a later Present.
  if (!g_native_capture.readback_copy_issued) {
    context->CopyResource(g_native_capture.staging,
                          g_native_capture.render_texture);
    // Copy depth second and fence both copies with an event query. Present may
    // poll that event without flushing or waiting; ordinary staging Map is
    // used only after the GPU explicitly reports completion.
    context->CopyResource(g_native_capture.depth_staging,
                          g_native_capture.depth_texture);
    context->End(g_native_capture.readback_query);
    g_native_capture.readback_copy_issued = true;
    if (timed_out) context->Flush();
    context->Release();
    return;
  }
  BOOL query_complete = FALSE;
  const HRESULT query_result = context->GetData(
      g_native_capture.readback_query, &query_complete,
      sizeof(query_complete),
      timed_out ? 0 : D3D11_ASYNC_GETDATA_DONOTFLUSH);
  if (query_result == S_FALSE ||
      (query_result == S_OK && !query_complete)) {
    if (timed_out) context->Flush();
    if (readback_age >= kNativeReadbackAbortMs) {
      PluginLog(false,
                "[GPU] [ITEM_RENDER] readback aborted form=%08X "
                "age=%llums; renderer resources will be recreated",
                g_native_capture.request.form_id,
                static_cast<unsigned long long>(readback_age));
      context->Release();
      finish_failed_readback();
      g_native_render_reset_revision.fetch_add(1,
                                                std::memory_order_acq_rel);
      return;
    }
    context->Release();
    return;
  }
  if (!same_request || device_changed) {
    context->Release();
    g_native_capture.readback_pending = false;
    g_native_capture.readback_copy_issued = false;
    g_native_capture.readback_dispatched_ms = 0;
    g_native_capture.readback_ready = false;
    g_native_capture.readback_success = false;
    PluginLog(false,
              "[GPU] [ITEM_RENDER] stale readback drained form=%08X "
              "query=%08lX same_request=%u device_changed=%u",
              g_native_capture.request.form_id,
              static_cast<unsigned long>(query_result),
              same_request ? 1u : 0u, device_changed ? 1u : 0u);
    return;
  }
  D3D11_MAPPED_SUBRESOURCE mapped_depth = {};
  const HRESULT depth_map_result = SUCCEEDED(query_result)
      ? context->Map(g_native_capture.depth_staging, 0, D3D11_MAP_READ, 0,
                     &mapped_depth)
      : query_result;
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  HRESULT map_result = E_PENDING;
  if (SUCCEEDED(depth_map_result)) {
    map_result = context->Map(g_native_capture.staging, 0, D3D11_MAP_READ, 0,
                              &mapped);
  }
  bool converted = false;
  if (SUCCEEDED(depth_map_result) && SUCCEEDED(map_result)) {
    converted = ConvertNativeCapture(mapped, mapped_depth,
                                     g_native_capture.width,
                                     g_native_capture.height,
                                     g_native_capture.format);
    context->Unmap(g_native_capture.staging, 0);
  }
  if (SUCCEEDED(depth_map_result))
    context->Unmap(g_native_capture.depth_staging, 0);
  if (FAILED(depth_map_result) || FAILED(map_result)) {
    const HRESULT removed_reason = g_native_capture.resource_device
        ? g_native_capture.resource_device->GetDeviceRemovedReason()
        : E_POINTER;
    PluginLog(false,
              "[GPU] [ITEM_RENDER] Present map failed form=%08X "
              "query=%08lX color=%08lX depth=%08lX removed=%08lX "
              "age=%llums",
              g_native_capture.request.form_id,
              static_cast<unsigned long>(query_result),
              static_cast<unsigned long>(map_result),
              static_cast<unsigned long>(depth_map_result),
              static_cast<unsigned long>(removed_reason),
              static_cast<unsigned long long>(readback_age));
    if (FAILED(removed_reason))
      g_native_render_reset_revision.fetch_add(1,
                                                std::memory_order_acq_rel);
  }
  context->Release();
  g_native_capture.readback_pending = false;
  g_native_capture.readback_copy_issued = false;
  g_native_capture.readback_dispatched_ms = 0;
  g_native_capture.readback_success = converted;
  g_native_capture.readback_ready = true;
  g_native_capture.engine_render_result = 1;
  if (g_native_capture.capture_attempts <= 2 || converted) {
    PluginLog(false,
              "[GPU] [ITEM_RENDER] Present readback form=%08X age=%llums "
              "map=%08lX depth_map=%08lX converted=%u foreground=%u "
              "background=%u depth=%u alpha=%u rgb_only=%u alpha_range=%u..%u",
              g_native_capture.request.form_id,
              static_cast<unsigned long long>(GetTickCount64() -
                                              dispatched_ms),
              static_cast<unsigned long>(map_result),
              static_cast<unsigned long>(depth_map_result),
              converted ? 1u : 0u,
              g_native_capture.content_samples,
              g_native_capture.background_samples,
              g_native_capture.depth_samples,
              g_native_capture.alpha_samples,
              g_native_capture.rgb_without_alpha_samples,
              g_native_capture.minimum_alpha,
              g_native_capture.maximum_alpha);
  }
}

void ServiceCharacterPresentReadback(IDXGISwapChain* chain) {
  if (!g_character_render.readback_pending) return;
  const uint64_t dispatched_ms =
      g_character_render.readback_dispatched_ms;
  auto cancel_readback = [&]() {
    g_character_render.readback_pending = false;
    g_character_render.readback_copy_issued = false;
    g_character_render.readback_dispatched_ms = 0;
    g_character_render.readback_ready = false;
    g_character_render.readback_success = false;
    g_character_render.readback_world_epoch = 0;
    g_character_render.readback_source_revision = 0;
    g_character_render.readback_appearance_signature = 0;
    g_character_render.readback_reset_revision = 0;
    g_character_inflight_signature.store(0, std::memory_order_release);
    g_character_inflight_world_epoch.store(0, std::memory_order_release);
  };
  auto finish_failed_readback = [&]() {
    g_character_render.readback_pending = false;
    g_character_render.readback_copy_issued = false;
    g_character_render.readback_dispatched_ms = 0;
    g_character_render.readback_success = false;
    g_character_render.readback_ready = true;
    g_character_render.engine_render_result = 1;
  };
  if (!g_character_render.device || !g_character_render.readback_query ||
      (!g_character_render.readback_copy_issued &&
       (!g_character_render.render_texture || !g_character_render.staging ||
        !g_character_render.depth_texture ||
        !g_character_render.depth_staging))) {
    cancel_readback();
    return;
  }
  const uint64_t readback_age =
      dispatched_ms ? GetTickCount64() - dispatched_ms : 0;
  const bool timed_out = dispatched_ms &&
                         readback_age >= kNativeReadbackTimeoutMs;
  const bool same_source =
      g_character_render.readback_world_epoch != 0 &&
      g_character_render.readback_source_revision != 0 &&
      g_character_render.readback_world_epoch ==
          g_world_epoch.load(std::memory_order_acquire) &&
      g_character_render.readback_reset_revision ==
          g_character_reset_revision.load(std::memory_order_acquire) &&
      LiveWorldReadyForNativeRendering();

  ID3D11Device* chain_device = nullptr;
  const HRESULT chain_device_result =
      chain ? chain->GetDevice(__uuidof(ID3D11Device),
                               reinterpret_cast<void**>(&chain_device))
            : E_POINTER;
  const bool chain_device_available =
      SUCCEEDED(chain_device_result) && chain_device;
  const bool device_changed =
      !chain_device_available || chain_device != g_character_render.device;
  if (device_changed) {
    PluginLog(false,
              "[GPU] [CHAR_PREVIEW] device changed before Present readback "
              "cached=%p current=%p chain_hr=%08lX; draining "
              "cached-device query",
              g_character_render.device, chain_device,
              static_cast<unsigned long>(chain_device_result));
    g_native_render_reset_revision.fetch_add(1, std::memory_order_acq_rel);
  }
  if (chain_device) chain_device->Release();
  ID3D11DeviceContext* context = nullptr;
  g_character_render.device->GetImmediateContext(&context);
  if (!context) {
    PluginLog(false,
              "[GPU] [CHAR_PREVIEW] cached device has no immediate context "
              "removed=%08lX",
              static_cast<unsigned long>(
                  g_character_render.device->GetDeviceRemovedReason()));
    g_native_render_reset_revision.fetch_add(1, std::memory_order_acq_rel);
    finish_failed_readback();
    return;
  }

  if (!g_character_render.readback_copy_issued) {
    context->CopyResource(g_character_render.staging,
                          g_character_render.render_texture);
    context->CopyResource(g_character_render.depth_staging,
                          g_character_render.depth_texture);
    context->End(g_character_render.readback_query);
    g_character_render.readback_copy_issued = true;
    if (timed_out) context->Flush();
    context->Release();
    return;
  }
  BOOL query_complete = FALSE;
  const HRESULT query_result = context->GetData(
      g_character_render.readback_query, &query_complete,
      sizeof(query_complete),
      timed_out ? 0 : D3D11_ASYNC_GETDATA_DONOTFLUSH);
  if (query_result == S_FALSE ||
      (query_result == S_OK && !query_complete)) {
    if (timed_out) {
      context->Flush();
      if (readback_age < kNativeReadbackTimeoutMs + 1000) {
        PluginLog(false,
                  "[GPU] [CHAR_PREVIEW] readback timeout draining "
                  "age=%llums revision=%llu",
                  static_cast<unsigned long long>(readback_age),
                  static_cast<unsigned long long>(
                      g_character_render.readback_source_revision));
      }
    }
    context->Release();
    return;
  }
  if (!same_source || device_changed) {
    context->Release();
    g_character_render.readback_pending = false;
    g_character_render.readback_copy_issued = false;
    g_character_render.readback_dispatched_ms = 0;
    g_character_render.readback_ready = false;
    g_character_render.readback_success = false;
    g_character_render.readback_world_epoch = 0;
    g_character_render.readback_source_revision = 0;
    g_character_render.readback_appearance_signature = 0;
    g_character_render.readback_reset_revision = 0;
    g_character_inflight_signature.store(0, std::memory_order_release);
    g_character_inflight_world_epoch.store(0, std::memory_order_release);
    PluginLog(false,
              "[GPU] [CHAR_PREVIEW] stale readback drained query=%08lX "
              "same_source=%u device_changed=%u",
              static_cast<unsigned long>(query_result),
              same_source ? 1u : 0u, device_changed ? 1u : 0u);
    return;
  }
  D3D11_MAPPED_SUBRESOURCE mapped_depth = {};
  const HRESULT depth_map_result = SUCCEEDED(query_result)
      ? context->Map(g_character_render.depth_staging, 0, D3D11_MAP_READ, 0,
                     &mapped_depth)
      : query_result;
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  const HRESULT map_result = SUCCEEDED(depth_map_result)
      ? context->Map(g_character_render.staging, 0, D3D11_MAP_READ, 0,
                     &mapped)
      : depth_map_result;
  bool converted = false;
  if (SUCCEEDED(depth_map_result) && SUCCEEDED(map_result)) {
    converted = ConvertCharacterFrame(mapped, mapped_depth);
    context->Unmap(g_character_render.staging, 0);
  }
  if (SUCCEEDED(depth_map_result))
    context->Unmap(g_character_render.depth_staging, 0);
  if (FAILED(depth_map_result) || FAILED(map_result)) {
    const HRESULT removed_reason = g_character_render.device
        ? g_character_render.device->GetDeviceRemovedReason()
        : E_POINTER;
    PluginLog(false,
              "[GPU] [CHAR_PREVIEW] Present map failed query=%08lX "
              "map=%08lX depth_map=%08lX removed=%08lX age=%llums "
              "revision=%llu",
              static_cast<unsigned long>(query_result),
              static_cast<unsigned long>(map_result),
              static_cast<unsigned long>(depth_map_result),
              static_cast<unsigned long>(removed_reason),
              static_cast<unsigned long long>(readback_age),
              static_cast<unsigned long long>(
                  g_character_render.readback_source_revision));
    if (FAILED(removed_reason))
      g_native_render_reset_revision.fetch_add(1,
                                                std::memory_order_acq_rel);
  }
  context->Release();
  g_character_render.readback_pending = false;
  g_character_render.readback_copy_issued = false;
  g_character_render.readback_dispatched_ms = 0;
  g_character_render.readback_success = converted;
  g_character_render.readback_ready = true;
  g_character_render.engine_render_result = 1;
  if (g_character_render.render_attempts <= 3 || converted) {
    PluginLog(false,
              "[GPU] [CHAR_PREVIEW] Present readback age=%llums map=%08lX "
              "depth_map=%08lX converted=%u foreground=%u rgb=%u..%u",
              static_cast<unsigned long long>(GetTickCount64() -
                                              dispatched_ms),
              static_cast<unsigned long>(map_result),
              static_cast<unsigned long>(depth_map_result),
              converted ? 1u : 0u,
              g_character_render.foreground_pixels,
              g_character_render.minimum_rgb,
              g_character_render.maximum_rgb);
  }
}

bool NativeRenderWorkPending(bool include_map) {
  if (g_native_render_reset_revision.load(std::memory_order_acquire) !=
      g_observed_native_render_reset_revision)
    return true;
  if (g_native_preview_queue_depth.load(std::memory_order_acquire) != 0 ||
      g_native_active_preview_form.load(std::memory_order_acquire) != 0 ||
      g_native_capture_equipment_preempt_requested.load(
          std::memory_order_acquire))
    return true;
  if (include_map &&
      g_native_map_requested_until_ms.load(std::memory_order_acquire) != 0)
    return true;
  if (g_custom_character_preview_enabled && g_get_character_surface) {
    dis::CharacterPreviewSurface surface{};
    if (g_get_character_surface(&surface) &&
        dis::player_preview::NeedsPresent(surface))
      return true;
  }
  return g_native_character_preview_enabled &&
         g_character_surface_visible.load(std::memory_order_acquire);
}

HRESULT __stdcall ItemPreviewPresentHook(IDXGISwapChain* chain, UINT interval,
                                         UINT flags) {
  PresentFn original =
      g_capture_present_original.load(std::memory_order_acquire);
  if (!original) return S_OK;
  if (!g_inside_capture_present && NativeRenderWorkPending(true) &&
      IsPrimaryCaptureChain(chain)) {
    // Serialize Present against HUD UI3D submission and restore the short
    // character-only framebuffer lease before any copy, reset, map capture,
    // or real swapchain presentation can observe it.
    AcquireSRWLockExclusive(&g_native_render_lock);
    g_inside_capture_present = true;
    try {
      RestoreDeferredUi3DTarget(chain);
      if (g_custom_character_preview_enabled && g_get_character_surface &&
          !g_equipment_mutation_in_progress.load(std::memory_order_acquire) &&
          !g_equipment_settlement_active.load(std::memory_order_acquire)) {
        dis::CharacterPreviewSurface surface{};
        if (g_get_character_surface(&surface) && surface.visible)
          dis::player_preview::RenderPresent(chain, surface);
      }
      ServiceCharacterPresentReadback(chain);
      ServiceNativeMapPresentReadback(chain);
      // Present is the completion point for the ordered draw/copy/query.  Hand
      // the finished portrait to WEMU before retiring the transient Skyrim
      // graph; waiting for the next HUD callback left a valid converted frame
      // invisible and made a native destructor failure look like a render
      // failure to the player.
      ConsumeCompletedCharacterReadback(true);
      RetireSubmittedCharacterSceneAtPresent();
      if (FindOpenInventoryMenu() && CharacterCloneLifecycleQuiescent() &&
          (g_character_render.active_clone || PendingCharacterPointer() ||
           g_character_render.scheme_session_owned)) {
        void* scene_manager = ReadOr<void*>(
            reinterpret_cast<void*>(g_base + kUI3DSceneManagerPtr), nullptr);
        RetireCharacterSceneObjects(
            scene_manager, "native InventoryMenu Present quarantine", true);
      }
      ServiceInventoryPresentReadback(chain);
      if (FindOpenInventoryMenu()) {
        void* inventory_manager = ReadOr<void*>(
            reinterpret_cast<void*>(g_base + kInventory3DManagerPtr),
            nullptr);
        ServiceNativeInventoryMenuHandoff(inventory_manager,
                                          "Present ownership gate");
      }
      const bool reset_complete = ServiceNativeRenderReset();
      (void)reset_complete;
    } catch (...) {
      EndUi3DFrameTrace();
      RestoreDeferredUi3DTarget(chain, "Present exception");
      PluginLog(false,
                "native-render: C++ exception; render-thread reset requested");
      g_native_render_reset_revision.fetch_add(1,
                                                std::memory_order_acq_rel);
    }
    g_inside_capture_present = false;
    ReleaseSRWLockExclusive(&g_native_render_lock);
  }
  return original(chain, interval, flags);
}

bool InstallItemPreviewPresentHook() {
  if (g_capture_present_hooked.load(std::memory_order_acquire)) return true;
  auto* chain = CurrentPrimarySwapChain();
  if (!chain || !RegionAllows(chain, sizeof(void*))) return false;
  void** table = ReadOr<void**>(chain, nullptr);
  if (!table || !RegionAllows(table + 8, sizeof(void*))) return false;
  void* current = ReadOr<void*>(table + 8, nullptr);
  if (current == reinterpret_cast<void*>(&ItemPreviewPresentHook)) {
    const bool valid =
        g_capture_present_original.load(std::memory_order_acquire) != nullptr;
    if (valid) {
      g_primary_capture_chain.store(chain, std::memory_order_release);
      g_capture_present_hooked.store(true, std::memory_order_release);
    }
    return valid;
  }
  if (!RegionAllows(current, 1, true)) return false;
  DWORD old = 0;
  if (!VirtualProtect(table + 8, sizeof(void*), PAGE_EXECUTE_READWRITE, &old))
    return false;
  g_capture_present_original.store(reinterpret_cast<PresentFn>(current),
                                   std::memory_order_release);
  g_primary_capture_chain.store(chain, std::memory_order_release);
  void* const previous = InterlockedCompareExchangePointer(
      reinterpret_cast<void* volatile*>(table + 8),
      reinterpret_cast<void*>(&ItemPreviewPresentHook), current);
  DWORD ignored = 0;
  const BOOL restored =
      VirtualProtect(table + 8, sizeof(void*), old, &ignored);
  if (previous != current) {
    g_capture_present_original.store(nullptr, std::memory_order_release);
    g_primary_capture_chain.store(nullptr, std::memory_order_release);
    PluginLog(false,
              "native-render: Present hook race current=%p previous=%p",
              current, previous);
    return false;
  }
  FlushInstructionCache(GetCurrentProcess(), table + 8, sizeof(void*));
  g_capture_present_hooked.store(true, std::memory_order_release);
  if (!restored)
    PluginLog(false,
              "native-render: Present hook installed but protection restore "
              "failed error=%lu",
              static_cast<unsigned long>(GetLastError()));
  PluginLog(false, "native-render: main swapchain present hook installed chain=%p",
            chain);
  return true;
}

void HudPostDisplayHook(void* menu) {
  HudPostDisplayFn original =
      g_hud_post_display_original.load(std::memory_order_acquire);
  if (original) original(menu);

  if (g_inside_hud_post_display ||
      !g_hud_post_display_hooked.load(std::memory_order_acquire) ||
      g_equipment_mutation_in_progress.load(std::memory_order_acquire) ||
      g_equipment_settlement_active.load(std::memory_order_acquire) ||
      !LiveWorldReadyForNativeRendering() ||
      !NativeRenderWorkPending(false))
    return;

  IDXGISwapChain* chain = CurrentPrimarySwapChain();
  if (!chain || !TryAcquireSRWLockExclusive(&g_native_render_lock)) return;
  if (g_equipment_mutation_in_progress.load(std::memory_order_acquire) ||
      g_equipment_settlement_active.load(std::memory_order_acquire)) {
    ReleaseSRWLockExclusive(&g_native_render_lock);
    return;
  }

  g_inside_hud_post_display = true;
  try {
    static std::atomic<bool> first_callback_logged{false};
    if (!first_callback_logged.exchange(true, std::memory_order_acq_rel)) {
      PluginLog(false,
                "[GPU] native-render: first HUD PostDisplay callback "
                "menu=%p chain=%p thread=%lu",
                menu, chain,
                static_cast<unsigned long>(GetCurrentThreadId()));
    }
    const bool reset_complete = ServiceNativeRenderReset();
    const UiRendererCounters menu_counters = ReadUiRendererCounters();
    if (reset_complete && LiveWorldReadyForNativeRendering() &&
        menu_counters.disable_pause == 0) {
      // HUDMenu::PostDisplay executes in the same UI render phase used by
      // InventoryMenu's native Inventory3DManager preview. This is early
      // enough for UI3DSceneManager to submit real draw work safely.
      const uint64_t now = GetTickCount64();
      const bool character_priority =
          g_character_surface_visible.load(std::memory_order_acquire) &&
          (!g_character_render.has_good_frame ||
           now < g_character_live_transition_until_ms.load(
                     std::memory_order_acquire));
      const bool item_transaction_active =
          g_native_capture.stage != NativeCaptureStage::kIdle;
      if (character_priority && !item_transaction_active) {
        // The portrait needs at most one lease for this frame. Inventory/magic
        // preloading immediately continues whenever no actor clone is ready.
        // Never preempt an already-started Inventory3D transaction: doing so
        // abandoned its retry after the character Present and left every later
        // FormID permanently queued.
        TickCharacterPreview(chain);
        if (!g_deferred_ui3d_target.active)
          TickNativeCapture(chain);
      } else {
        // An asynchronous Skyrim item load/render/readback is one atomic serial
        // transaction. Finish it before lending UI3D to the character renderer.
        TickNativeCapture(chain);
        if (!g_deferred_ui3d_target.active)
          TickCharacterPreview(chain);
      }
    }
  } catch (...) {
    EndUi3DFrameTrace();
    RestoreDeferredUi3DTarget(chain, "HUD exception");
    PluginLog(false,
              "native-render: C++ exception in HUD PostDisplay; "
              "render-thread reset requested");
    g_native_render_reset_revision.fetch_add(1,
                                              std::memory_order_acq_rel);
  }
  g_inside_hud_post_display = false;
  ReleaseSRWLockExclusive(&g_native_render_lock);
}

bool InstallHudPostDisplayHook() {
  if (g_hud_post_display_hooked.load(std::memory_order_acquire)) return true;
  void** table = reinterpret_cast<void**>(g_base + kHudMenuVtable);
  void** slot = table + kHudPostDisplaySlot;
  if (!RegionAllows(slot, sizeof(void*))) return false;
  void* current = ReadOr<void*>(slot, nullptr);
  if (current == reinterpret_cast<void*>(&HudPostDisplayHook)) {
    const bool valid =
        g_hud_post_display_original.load(std::memory_order_acquire) != nullptr;
    if (valid)
      g_hud_post_display_hooked.store(true, std::memory_order_release);
    return valid;
  }
  if (!RegionAllows(current, 1, true)) return false;

  DWORD old = 0;
  if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old))
    return false;
  g_hud_post_display_original.store(
      reinterpret_cast<HudPostDisplayFn>(current),
      std::memory_order_release);
  void* const previous = InterlockedCompareExchangePointer(
      reinterpret_cast<void* volatile*>(slot),
      reinterpret_cast<void*>(&HudPostDisplayHook), current);
  DWORD ignored = 0;
  const BOOL restored = VirtualProtect(slot, sizeof(void*), old, &ignored);
  if (previous != current) {
    g_hud_post_display_original.store(nullptr, std::memory_order_release);
    PluginLog(false,
              "native-render: HUD PostDisplay hook race current=%p "
              "previous=%p",
              current, previous);
    return false;
  }
  FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
  g_hud_post_display_hooked.store(true, std::memory_order_release);
  if (!restored) {
    PluginLog(false,
              "native-render: HUD PostDisplay hook installed but protection "
              "restore failed error=%lu",
              static_cast<unsigned long>(GetLastError()));
  }
  PluginLog(false,
            "native-render: HUD PostDisplay hook installed vtable=%p "
            "slot=%llu original=%p",
            table, static_cast<unsigned long long>(kHudPostDisplaySlot),
            current);
  return true;
}

void InventoryPostDisplayHook(void* menu) {
  InventoryPostDisplayFn original =
      g_inventory_post_display_original.load(std::memory_order_acquire);
  if (!original) return;
  g_native_inventory_last_display_ms.store(GetTickCount64(),
                                            std::memory_order_release);
  if (g_inside_inventory_post_display ||
      !g_inventory_post_display_hooked.load(std::memory_order_acquire)) {
    original(menu);
    return;
  }

  // Inventory3DManager and the character UI3D scene are process-global native
  // resources. Hand both back before entering Skyrim's real menu callback. If
  // a deferred draw/readback is still ordered ahead of us, defer only this
  // PostDisplay invocation; Present drains it and the next menu frame proceeds
  // with a neutral singleton. Calling the original during that interval was
  // the verified custom/native ownership collision behind weapon switching.
  bool native_display_safe = false;
  if (TryAcquireSRWLockExclusive(&g_native_render_lock)) {
    native_display_safe = true;
    if (g_native_character_preview_enabled &&
        (g_character_render.active_clone || PendingCharacterPointer() ||
         g_character_render.scheme_session_owned)) {
      void* character_scene_manager = ReadOr<void*>(
          reinterpret_cast<void*>(g_base + kUI3DSceneManagerPtr), nullptr);
      native_display_safe = RetireCharacterSceneObjects(
          character_scene_manager, "native InventoryMenu ownership", true);
    }
    if (native_display_safe && g_native_item_models_enabled &&
        g_native_capture.stage != NativeCaptureStage::kIdle) {
      void* inventory_manager = ReadOr<void*>(
          reinterpret_cast<void*>(g_base + kInventory3DManagerPtr), nullptr);
      native_display_safe = ServiceNativeInventoryMenuHandoff(
          inventory_manager, "InventoryMenu pre-PostDisplay");
    }
    ReleaseSRWLockExclusive(&g_native_render_lock);
  }
  if (!native_display_safe) {
    static uint64_t last_deferred_log_ms = 0;
    const uint64_t now = GetTickCount64();
    if (!last_deferred_log_ms || now - last_deferred_log_ms >= 1000) {
      PluginLog(false,
                "[ITEM_LIFECYCLE] deferred native InventoryMenu PostDisplay "
                "until custom UI3D ownership is neutral stage=%u char=%p "
                "lease=%u item_readback=%u",
                static_cast<unsigned>(g_native_capture.stage),
                g_character_render.active_clone,
                g_deferred_ui3d_target.active ? 1u : 0u,
                g_native_capture.readback_pending ? 1u : 0u);
      last_deferred_log_ms = now;
    }
    return;
  }

  g_inside_inventory_post_display = true;
  void* manager = ReadOr<void*>(
      reinterpret_cast<void*>(g_base + kInventory3DManagerPtr), nullptr);
  void* scene_manager = ReadOr<void*>(
      reinterpret_cast<void*>(g_base + kUI3DSceneManagerPtr), nullptr);
  const uint32_t prior =
      g_native_inventory_control_traces.load(std::memory_order_acquire);
  bool trace = prior < 2 && manager && scene_manager &&
               RegionAllows(manager, 0x158) &&
               RegionAllows(scene_manager, 0x118) &&
               InstallUi3DTraceHooks(scene_manager);
  void* scene_root = nullptr;
  int32_t scheme = -1;
  if (trace) {
    scheme = ReadOr<int32_t>(reinterpret_cast<uint8_t*>(manager) + 0x34, -1);
    if (scheme >= 0 && scheme < 8) {
      scene_root = ReadOr<void*>(reinterpret_cast<uint8_t*>(scene_manager) +
                                    0x38 + scheme * sizeof(void*),
                                nullptr);
    }
    trace = scene_root && RegionAllows(scene_root, 0x110);
  }
  if (trace) {
    g_native_inventory_control_traces.fetch_add(1, std::memory_order_acq_rel);
    PluginLog(false,
              "[ITEM_UI3D] native control begin menu=%p manager=%p "
              "scheme=%d scene_root=%p disable_pause=%u custom=%u",
              menu, manager, scheme, scene_root,
              ReadUiRendererCounters().disable_pause,
              ReadUiRendererCounters().custom_rendering);
    BeginUi3DFrameTrace(DeferredUi3DTargetOwner::kNativeInventory,
                        scene_manager, scene_root, nullptr);
  }
  original(menu);
  if (trace) {
    EndUi3DFrameTrace();
    PluginLog(false, "[ITEM_UI3D] native control end menu=%p scheme=%d",
              menu, scheme);
  }
  g_inside_inventory_post_display = false;
}

bool InstallInventoryPostDisplayHook() {
  if (g_inventory_post_display_hooked.load(std::memory_order_acquire))
    return true;
  void** table = reinterpret_cast<void**>(g_base + kInventoryMenuVtable);
  void** slot = table + kInventoryPostDisplaySlot;
  if (!RegionAllows(slot, sizeof(void*))) return false;
  void* current = ReadOr<void*>(slot, nullptr);
  if (current == reinterpret_cast<void*>(&InventoryPostDisplayHook)) {
    const bool valid =
        g_inventory_post_display_original.load(std::memory_order_acquire) !=
        nullptr;
    if (valid)
      g_inventory_post_display_hooked.store(true, std::memory_order_release);
    return valid;
  }
  if (!RegionAllows(current, 1, true)) return false;
  DWORD old = 0;
  if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old))
    return false;
  g_inventory_post_display_original.store(
      reinterpret_cast<InventoryPostDisplayFn>(current),
      std::memory_order_release);
  void* previous = InterlockedCompareExchangePointer(
      reinterpret_cast<void* volatile*>(slot),
      reinterpret_cast<void*>(&InventoryPostDisplayHook), current);
  DWORD ignored = 0;
  VirtualProtect(slot, sizeof(void*), old, &ignored);
  if (previous != current) {
    g_inventory_post_display_original.store(nullptr,
                                            std::memory_order_release);
    return false;
  }
  FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
  g_inventory_post_display_hooked.store(true, std::memory_order_release);
  PluginLog(false,
            "[ITEM_UI3D] native InventoryMenu control hook installed "
            "vtable=%p slot=%llu original=%p",
            table,
            static_cast<unsigned long long>(kInventoryPostDisplaySlot),
            current);
  return true;
}

struct GameString {
  char* data = nullptr;
  uint16_t size = 0;
  uint16_t capacity = 0;
  uint32_t padding = 0;
};
static_assert(sizeof(GameString) == 0x10, "Skyrim BSString layout changed");

void ReleaseGameString(GameString& value) {
  if (!value.data) return;
  using SingletonFn = void*(*)();
  using DeallocateFn = void(*)(void*, void*, bool);
  auto singleton = reinterpret_cast<SingletonFn>(g_base + kGetMemoryManager);
  auto deallocate = reinterpret_cast<DeallocateFn>(g_base + kMemoryManagerDeallocate);
  if (RegionAllows(reinterpret_cast<void*>(singleton), 1, true) &&
      RegionAllows(reinterpret_cast<void*>(deallocate), 1, true)) {
    void* manager = singleton();
    if (manager) deallocate(manager, value.data, false);
  }
  value = {};
}

void StripMarkup(char* text) {
  if (!text) return;
  char* read = text;
  char* write = text;
  bool in_tag = false;
  bool previous_space = false;
  while (*read) {
    const char c = *read++;
    if (c == '<') { in_tag = true; continue; }
    if (in_tag) {
      if (c == '>') in_tag = false;
      continue;
    }
    const bool whitespace = c == '\r' || c == '\n' || c == '\t' || c == ' ';
    if (whitespace) {
      if (!previous_space && write != text) *write++ = ' ';
      previous_space = true;
    } else {
      *write++ = c;
      previous_space = false;
    }
  }
  while (write > text && write[-1] == ' ') --write;
  *write = 0;
}

bool CopyComponentDescription(void* form, size_t component_offset,
                              uint32_t field_type, char* output,
                              size_t output_size) {
  if (!form || !output || output_size == 0 ||
      !RegionAllows(reinterpret_cast<uint8_t*>(form) + component_offset, 0x10))
    return false;
  using GetDescriptionFn = void(*)(void*, GameString*, void*, uint32_t);
  auto get_description = reinterpret_cast<GetDescriptionFn>(g_base + kGetDescription);
  if (!RegionAllows(reinterpret_cast<void*>(get_description), 1, true)) return false;
  GameString value = {};
  get_description(reinterpret_cast<uint8_t*>(form) + component_offset,
                  &value, form, field_type);
  CopyText(output, output_size, value.data);
  ReleaseGameString(value);
  StripMarkup(output);
  return output[0] != 0;
}

void ReplaceAll(std::string& text, const char* token, const char* replacement) {
  if (!token || !*token || !replacement) return;
  const size_t token_size = std::strlen(token);
  size_t position = 0;
  while ((position = text.find(token, position)) != std::string::npos) {
    text.replace(position, token_size, replacement);
    position += std::strlen(replacement);
  }
}

bool CopyEffectDescription(void* magic_item, char* output, size_t output_size) {
  if (!magic_item || !output || output_size == 0) return false;
  void** effects = ReadOr<void**>(reinterpret_cast<uint8_t*>(magic_item) + 0x58, nullptr);
  const uint32_t count = std::min<uint32_t>(
      ReadOr<uint32_t>(reinterpret_cast<uint8_t*>(magic_item) + 0x60, 0), 16);
  if (!effects || !count ||
      !RegionAllows(effects, static_cast<size_t>(count) * sizeof(void*))) return false;
  std::string combined;
  for (uint32_t index = 0; index < count && combined.size() < output_size - 1; ++index) {
    void* effect = ReadOr<void*>(effects + index, nullptr);
    void* base_effect = effect
        ? ReadOr<void*>(reinterpret_cast<uint8_t*>(effect) + 0x10, nullptr)
        : nullptr;
    if (!effect || !base_effect || !RegionAllows(effect, 0x20) ||
        !RegionAllows(base_effect, 0x188)) continue;
    const char* raw = ReadOr<const char*>(
        reinterpret_cast<uint8_t*>(base_effect) + 0x180, nullptr);
    char safe[256] = {};
    CopyText(safe, sizeof(safe), raw);
    if (!safe[0]) CopyText(safe, sizeof(safe), FormName(base_effect));
    if (!safe[0]) continue;
    char magnitude[32] = {};
    char duration[32] = {};
    char area[32] = {};
    std::snprintf(magnitude, sizeof(magnitude), "%.0f",
                  ReadOr<float>(effect, 0.0f));
    std::snprintf(duration, sizeof(duration), "%u",
                  ReadOr<uint32_t>(reinterpret_cast<uint8_t*>(effect) + 8, 0));
    std::snprintf(area, sizeof(area), "%u",
                  ReadOr<uint32_t>(reinterpret_cast<uint8_t*>(effect) + 4, 0));
    std::string line(safe);
    ReplaceAll(line, "<mag>", magnitude);
    ReplaceAll(line, "<dur>", duration);
    ReplaceAll(line, "<area>", area);
    if (!combined.empty()) combined += ". ";
    combined += line;
  }
  CopyText(output, output_size, combined.c_str());
  StripMarkup(output);
  return output[0] != 0;
}

void FillFormDescription(void* form, char* output, size_t output_size) {
  if (!output || output_size == 0) return;
  output[0] = 0;
  if (!form) return;
  // GetDescription expects the reversed four-character record tag used by
  // Skyrim's little-endian loader: 'CSED' addresses DESC and 'MANC' CNAM.
  constexpr uint32_t kDesc = 0x43534544u;
  constexpr uint32_t kCnam = 0x4D414E43u;
  const uint8_t type = FormType(form);
  switch (type) {
    case 0x16: CopyComponentDescription(form, 0xB0, kDesc, output, output_size); break;
    case 0x77: CopyComponentDescription(form, 0x50, kDesc, output, output_size); break;
    case 0x29: CopyComponentDescription(form, 0x158, kDesc, output, output_size); break;
    case 0x1A: CopyComponentDescription(form, 0x1F0, kDesc, output, output_size); break;
    case 0x2A: CopyComponentDescription(form, 0xE8, kDesc, output, output_size); break;
    case 0x21: CopyComponentDescription(form, 0x110, kDesc, output, output_size); break;
    case 0x1B:
      if (!CopyComponentDescription(form, 0x128, kCnam, output, output_size))
        CopyComponentDescription(form, 0xA8, kDesc, output, output_size);
      break;
    default: break;
  }
  // Weapons and armour expose their gameplay effect through the embedded
  // TESEnchantableForm, not the record's optional DESC component.  Read the
  // real EnchantmentItem and append its resolved effect text exactly as the
  // native item card does.
  if (type == 0x29 || type == 0x1A) {
    const size_t enchantment_offset = type == 0x29 ? 0x90 : 0x58;
    void* enchantment = ReadOr<void*>(
        reinterpret_cast<uint8_t*>(form) + enchantment_offset, nullptr);
    char effects[256] = {};
    if (enchantment && CopyEffectDescription(enchantment, effects,
                                              sizeof(effects))) {
      if (output[0] && std::strcmp(output, effects) != 0) {
        const size_t used = std::strlen(output);
        if (used + 2 < output_size) {
          std::snprintf(output + used, output_size - used, ". %s", effects);
        }
      } else if (!output[0]) {
        CopyText(output, output_size, effects);
      }
    }
  }
  if (!output[0] &&
      (type == 0x15 || type == 0x16 || type == 0x17 ||
       type == 0x1E || type == 0x2E))
    CopyEffectDescription(form, output, output_size);
}

void* FindContainerChanges(void* player) {
  if (!player) return nullptr;
  // ExtraDataList::presence is at TESObjectREFR+0x78. Avoid walking the list
  // unless its authoritative bitfield says ExtraContainerChanges (0x15)
  // exists. This is the same guard used by ExtraDataList::GetByType.
  void* presence = ReadOr<void*>(reinterpret_cast<uint8_t*>(player) + 0x78,
                                 nullptr);
  if (!presence || !RegionAllows(presence, 0x18)) return nullptr;
  const uint8_t present = ReadOr<uint8_t>(
      reinterpret_cast<uint8_t*>(presence) + (0x15u >> 3), 0);
  if ((present & static_cast<uint8_t>(1u << (0x15u & 7))) == 0) return nullptr;

  void* node = ReadOr<void*>(reinterpret_cast<uint8_t*>(player) + 0x70, nullptr);
  for (int n = 0; node && n < 256; ++n) {
    if (!RegionAllows(node, 0x18)) break;
    void* table = ReadOr<void*>(node, nullptr);
    if (table == reinterpret_cast<void*>(
                     g_base + kExtraContainerChangesVtable))
      return node;
    void* next = ReadOr<void*>(reinterpret_cast<uint8_t*>(node) + 8, nullptr);
    if (next == node) break;
    node = next;
  }
  return nullptr;
}

void* EquippedObject(void* player, bool left) {
  void* process = ReadOr<void*>(reinterpret_cast<uint8_t*>(player) + 0xF0, nullptr);
  return process ? ReadOr<void*>(reinterpret_cast<uint8_t*>(process) + 0xF0 + (left ? 0 : 8), nullptr) : nullptr;
}

dis::EquipmentSlot EquipmentSlotFor(void* form, void* equipped_left,
                                    void* equipped_right, bool worn) {
  if (!form) return dis::EquipmentSlot::kNone;
  const uint8_t type = FormType(form);
  if (type == 0x1A && worn) {
    // TESObjectARMO::BGSBipedObjectForm is at +0x1B0 in 1.5.97 and the
    // BOD2 slot mask is the first field of BIPED_MODEL at +0x1B8.
    const uint32_t mask = ReadOr<uint32_t>(
        reinterpret_cast<uint8_t*>(form) + 0x1B8, 0);
    if (mask & (1u << 9)) return dis::EquipmentSlot::kShield;
    if (mask & ((1u << 0) | (1u << 1) | (1u << 12) | (1u << 13)))
      return dis::EquipmentSlot::kHead;
    if (mask & ((1u << 2) | (1u << 16) | (1u << 26)))
      return dis::EquipmentSlot::kBody;
    if (mask & ((1u << 3) | (1u << 4) | (1u << 28) | (1u << 29)))
      return dis::EquipmentSlot::kHands;
    if (mask & ((1u << 7) | (1u << 8) | (1u << 23) | (1u << 24)))
      return dis::EquipmentSlot::kFeet;
    if (mask & (1u << 6)) return dis::EquipmentSlot::kRing;
    if (mask & (1u << 5)) return dis::EquipmentSlot::kAmulet;
    return dis::EquipmentSlot::kOther;
  }
  if (form == equipped_right) return dis::EquipmentSlot::kRightHand;
  if (form == equipped_left) return dis::EquipmentSlot::kLeftHand;
  return worn ? dis::EquipmentSlot::kOther : dis::EquipmentSlot::kNone;
}

dis::ItemIconKind ItemIconFor(void* form) {
  if (!form) return dis::ItemIconKind::kMisc;
  const uint8_t type = FormType(form);
  if (type == 0x29) {
    const uint8_t weapon_type = ReadOr<uint8_t>(
        reinterpret_cast<uint8_t*>(form) + 0x19D, 0xFF);
    switch (weapon_type) {
      case 1: case 5: return dis::ItemIconKind::kSword;
      case 2: return dis::ItemIconKind::kDagger;
      case 3: case 6: return dis::ItemIconKind::kAxe;
      case 4: return dis::ItemIconKind::kMace;
      case 7: case 9: return dis::ItemIconKind::kBow;
      case 8: return dis::ItemIconKind::kStaff;
      default: return dis::ItemIconKind::kSword;
    }
  }
  if (type == 0x2A) return dis::ItemIconKind::kArrow;
  if (type == 0x1A) {
    const uint32_t mask = ReadOr<uint32_t>(
        reinterpret_cast<uint8_t*>(form) + 0x1B8, 0);
    if (mask & (1u << 9)) return dis::ItemIconKind::kShield;
    if (mask & ((1u << 0) | (1u << 1) | (1u << 12) | (1u << 13)))
      return dis::ItemIconKind::kHead;
    if (mask & ((1u << 2) | (1u << 16) | (1u << 26)))
      return dis::ItemIconKind::kBody;
    if (mask & ((1u << 3) | (1u << 4) | (1u << 28) | (1u << 29)))
      return dis::ItemIconKind::kHands;
    if (mask & ((1u << 7) | (1u << 8) | (1u << 23) | (1u << 24)))
      return dis::ItemIconKind::kFeet;
    if (mask & ((1u << 5) | (1u << 6)))
      return dis::ItemIconKind::kJewelry;
    return dis::ItemIconKind::kBody;
  }
  if (type == 0x2E) return dis::ItemIconKind::kPotion;
  if (type == 0x1E) return dis::ItemIconKind::kIngredient;
  if (type == 0x17 || type == 0x1B || type == 0x30)
    return dis::ItemIconKind::kBook;
  return dis::ItemIconKind::kMisc;
}

bool ExtraListHasType(void* extra_list, uint32_t type) {
  if (!extra_list || type >= 0xC0) return false;
  void* presence = ReadOr<void*>(reinterpret_cast<uint8_t*>(extra_list) + 8, nullptr);
  if (!presence) return false;
  const uint8_t bits = ReadOr<uint8_t>(reinterpret_cast<uint8_t*>(presence) + (type >> 3), 0);
  return (bits & static_cast<uint8_t>(1u << (type & 7))) != 0;
}

uint32_t ExtraDataType(void* extra);

[[maybe_unused]] void* FindExtraData(void* extra_list, uint32_t type) {
  void* node = extra_list ? ReadOr<void*>(extra_list, nullptr) : nullptr;
  for (unsigned visited = 0; node && visited < 128; ++visited) {
    if (!RegionAllows(node, 0x18)) break;
    if (ExtraDataType(node) == type) return node;
    void* next = ReadOr<void*>(reinterpret_cast<uint8_t*>(node) + 8, nullptr);
    if (next == node) break;
    node = next;
  }
  return nullptr;
}

void* ChooseEntryExtra(void* entry, bool prefer_worn, bool* worn_out = nullptr) {
  if (worn_out) *worn_out = false;
  void* list = entry ? ReadOr<void*>(reinterpret_cast<uint8_t*>(entry) + 8, nullptr) : nullptr;
  if (!list || !RegionAllows(list, 0x10)) return nullptr;
  void* fallback = nullptr;
  void* node = list;
  for (unsigned visited = 0; node && visited < 128; ++visited) {
    if (!RegionAllows(node, 0x10)) break;
    void* extra = ReadOr<void*>(node, nullptr);
    if (extra && RegionAllows(extra, 0x18)) {
      if (!fallback) fallback = extra;
      const bool worn = ExtraListHasType(extra, 0x16) || ExtraListHasType(extra, 0x17);
      if (worn) {
        if (worn_out) *worn_out = true;
        return extra;
      }
    }
    node = ReadOr<void*>(reinterpret_cast<uint8_t*>(node) + 8, nullptr);
  }
  return prefer_worn ? nullptr : fallback;
}

void* FindInventoryEntry(void* player, uint32_t form_id) {
  void* changes = FindContainerChanges(player);
  void* data = changes ? ReadOr<void*>(reinterpret_cast<uint8_t*>(changes) + 0x10, nullptr) : nullptr;
  void* list = data ? ReadOr<void*>(data, nullptr) : nullptr;
  if (!list) return nullptr;
  void* node = list;
  for (unsigned visited = 0; node && visited < 512; ++visited) {
    if (!RegionAllows(node, 0x10)) break;
    void* entry = ReadOr<void*>(node, nullptr);
    void* form = entry ? ReadOr<void*>(entry, nullptr) : nullptr;
    if (form && FormId(form) == form_id) return entry;
    node = ReadOr<void*>(reinterpret_cast<uint8_t*>(node) + 8, nullptr);
  }
  return nullptr;
}

[[maybe_unused]] bool ReuseCachedItemDescription(uint32_t form_id,
                                                 char* output,
                                                 size_t output_size) {
  if (!form_id || !output || output_size == 0) return false;
  for (uint32_t i = 0; i < g_item_cache_count; ++i) {
    if (g_item_cache[i].form_id == form_id &&
        g_item_cache[i].description[0]) {
      CopyText(output, output_size, g_item_cache[i].description);
      return output[0] != 0;
    }
  }
  return false;
}

constexpr uint32_t kInventoryScanLimit = 1024;

struct InventoryChangeScratch {
  void* entry = nullptr;
  void* form = nullptr;
  int32_t delta = 0;
  bool consumed = false;
};

struct InventoryCandidateScratch {
  void* form = nullptr;
  void* entry = nullptr;
  int32_t count = 0;
};

// These buffers are only used from the serialized player-update sampler.  BSS
// storage avoids adding roughly 50 KiB to Skyrim's update-thread stack.
InventoryChangeScratch g_inventory_changes[kInventoryScanLimit] = {};
InventoryCandidateScratch g_inventory_candidates[kInventoryScanLimit] = {};

int InventoryCategory(uint8_t type) {
  switch (type) {
    case 0x29: case 0x2A: return 1;  // weapons and ammunition
    case 0x1A: return 2;             // apparel
    case 0x2E: return 3;             // potions, poisons, and food
    case 0x1E: return 4;             // ingredients
    case 0x17: case 0x1B: case 0x30: return 5;  // scrolls, books, notes
    default: return 6;
  }
}

void AddInventoryCandidate(uint32_t& count, void* form, void* entry,
                           int64_t item_count) {
  if (!form || item_count <= 0) return;
  item_count = std::min<int64_t>(item_count, 0x7FFFFFFFll);
  for (uint32_t i = 0; i < count; ++i) {
    if (g_inventory_candidates[i].form != form) continue;
    const int64_t combined = static_cast<int64_t>(g_inventory_candidates[i].count) +
                             item_count;
    g_inventory_candidates[i].count = static_cast<int32_t>(
        std::min<int64_t>(combined, 0x7FFFFFFFll));
    if (!g_inventory_candidates[i].entry && entry)
      g_inventory_candidates[i].entry = entry;
    return;
  }
  if (count >= kInventoryScanLimit) {
    g_inventory_truncated = true;
    return;
  }
  g_inventory_candidates[count++] = {
      form, entry, static_cast<int32_t>(item_count)};
}

void AppendInventoryState(dis::Snapshot& state, void* player, void* form,
                          void* entry, int32_t count, void* equipped_left,
                          void* equipped_right) {
  if (!form || count <= 0) return;
  const uint32_t form_id = FormId(form);
  if (!form_id) return;
  // Skyrim.esm record 000001F4 is the internal WEAP "Unarmed". It has no
  // FULL name or MODL subrecord, and Inventory3DManager correctly makes no
  // model request for it. It can appear as the actor's equipped fallback but
  // is not a player inventory item, so do not expose it as an unrenderable
  // blank row in the bottom-screen inventory.
  if (form_id == 0x000001F4) return;
  if (form_id == 0x0000000F) {
    const int64_t gold = static_cast<int64_t>(state.gold) + count;
    state.gold = static_cast<int32_t>(std::min<int64_t>(gold, 0x7FFFFFFFll));
    return;
  }
  if (state.item_count >= dis::kMaxItems) {
    g_inventory_truncated = true;
    return;
  }

  dis::ItemState& out = state.items[state.item_count++];
  // The live snapshot persists between samples, so a slot can contain the
  // previous topology's record. Initialize only the slot being rebuilt rather
  // than clearing the complete bounded item array every 100 ms.
  out = {};
  out.form_id = form_id;
  out.form_type = FormType(form);
  out.count = count;
  out.icon_kind = ItemIconFor(form);
  bool worn = false;
  if (entry) ChooseEntryExtra(entry, true, &worn);
  // The full inventory walk owns persistent worn state only. Hand state is
  // projected in one place after both full and cached reads so the UI cannot
  // alternate between two subtly different representations every second.
  // Skyrim briefly publishes null/intermediate hand objects while changing a
  // weapon; those transient values are stabilized below as well.
  if (worn) out.flags |= dis::kItemEquipped;
  out.equip_slot = EquipmentSlotFor(form, nullptr, nullptr, worn);
  // InventoryEntryData::GenerateName is not a passive getter: Skyrim can
  // allocate ExtraTextDisplayData and splice it into the entry's extra-data
  // list. Telemetry must never mutate inventory state, especially during the
  // first post-load update, so use the stable base-form name instead.
  CopyText(out.name, sizeof(out.name), FormName(form));
  // The first live sample remains identity-only: it is the one update where
  // container changes have only just crossed the world-ready gate. On every
  // later full refresh, use the same runtime calculators as Skyrim's item card
  // instead of leaving all values at their zero-initialized placeholders.
  if (!g_first_live_sample) {
    using WeightFn = float(*)(void*);
    auto get_weight = reinterpret_cast<WeightFn>(g_base + kGetFormWeight);
    if (RegionAllows(reinterpret_cast<void*>(get_weight), 1, true)) {
      const float weight = get_weight(form);
      if (std::isfinite(weight) && weight >= 0.0f) out.weight = weight;
    }

    if (entry) {
      using ValueFn = int32_t(*)(void*);
      auto get_value = reinterpret_cast<ValueFn>(g_base + kGetEntryValue);
      if (RegionAllows(reinterpret_cast<void*>(get_value), 1, true))
        out.value = std::max(0, get_value(entry));

      if (out.form_type == 0x29 || out.form_type == 0x2A) {
        using DamageFn = float(*)(void*, void*);
        auto get_damage = reinterpret_cast<DamageFn>(g_base + kGetDamage);
        if (RegionAllows(reinterpret_cast<void*>(get_damage), 1, true)) {
          const float damage = get_damage(player, entry);
          if (std::isfinite(damage) && damage >= 0.0f) out.damage = damage;
        }
      } else if (out.form_type == 0x1A) {
        using ArmorFn = float(*)(void*, void*);
        auto get_armor = reinterpret_cast<ArmorFn>(g_base + kGetArmor);
        if (RegionAllows(reinterpret_cast<void*>(get_armor), 1, true)) {
          const float armor = get_armor(player, entry);
          if (std::isfinite(armor) && armor >= 0.0f) out.armor = armor;
        }
      }
    } else {
      using BaseValueFn = int32_t(*)(void*);
      auto get_base_value =
          reinterpret_cast<BaseValueFn>(g_base + kGetBaseValue);
      if (RegionAllows(reinterpret_cast<void*>(get_base_value), 1, true))
        out.value = std::max(0, get_base_value(form));
    }
  }
}

void FillInventory(dis::Snapshot& state, void* player) {
  if (g_first_live_sample)
    PluginLog(false, "inventory-safe: resolve container changes begin");
  void* changes = FindContainerChanges(player);
  void* data = changes ? ReadOr<void*>(reinterpret_cast<uint8_t*>(changes) + 0x10, nullptr) : nullptr;
  if (g_first_live_sample)
    PluginLog(false,
              "inventory-safe: changes resolved extra=%p changes=%p list=%p",
              changes, data, data ? ReadOr<void*>(data, nullptr) : nullptr);
  if (data)
    state.carry = ReadOr<float>(reinterpret_cast<uint8_t*>(data) + 0x10, 0.0f);
  void* equipped_left = EquippedObject(player, true);
  void* equipped_right = EquippedObject(player, false);

  uint32_t change_count = 0;
  void* node = data ? ReadOr<void*>(data, nullptr) : nullptr;
  for (uint32_t visited = 0; node && visited < kInventoryScanLimit; ++visited) {
    if (!RegionAllows(node, 0x10)) break;
    void* entry = ReadOr<void*>(node, nullptr);
    void* next = ReadOr<void*>(reinterpret_cast<uint8_t*>(node) + 8, nullptr);
    if (entry && RegionAllows(entry, 0x18)) {
      void* form = ReadOr<void*>(entry, nullptr);
      if (form && RegionAllows(form, 0x20) && change_count < kInventoryScanLimit) {
        g_inventory_changes[change_count++] = {
            entry, form,
            ReadOr<int32_t>(reinterpret_cast<uint8_t*>(entry) + 0x10, 0),
            false};
      }
    }
    if (next == node) break;
    node = next;
  }
  if (g_first_live_sample)
    PluginLog(false, "inventory-safe: change entries=%u", change_count);

  uint32_t candidate_count = 0;
  void* base_form = player
      ? ReadOr<void*>(reinterpret_cast<uint8_t*>(player) + 0x40, nullptr)
      : nullptr;
  void* container = AsContainer(base_form);
  void** base_entries = container
      ? ReadOr<void**>(reinterpret_cast<uint8_t*>(container) + 8, nullptr)
      : nullptr;
  uint32_t base_count = container
      ? std::min<uint32_t>(
            ReadOr<uint32_t>(reinterpret_cast<uint8_t*>(container) + 0x10, 0),
            kInventoryScanLimit)
      : 0;
  if (g_first_live_sample)
    PluginLog(false,
              "inventory-safe: base form=%p container=%p entries=%p count=%u",
              base_form, container, base_entries, base_count);
  if (base_entries && RegionAllows(base_entries,
                                   static_cast<size_t>(base_count) * sizeof(void*))) {
    for (uint32_t i = 0; i < base_count; ++i) {
      void* base_entry = ReadOr<void*>(base_entries + i, nullptr);
      if (!base_entry || !RegionAllows(base_entry, 0x18)) continue;
      void* form = ReadOr<void*>(reinterpret_cast<uint8_t*>(base_entry) + 8, nullptr);
      if (!form || !RegionAllows(form, 0x20)) continue;
      int64_t total = ReadOr<uint32_t>(base_entry, 0);
      void* representative_entry = nullptr;
      for (uint32_t j = 0; j < change_count; ++j) {
        InventoryChangeScratch& change = g_inventory_changes[j];
        if (change.form != form || change.consumed) continue;
        total += change.delta;
        change.consumed = true;
        if (!representative_entry) representative_entry = change.entry;
      }
      AddInventoryCandidate(candidate_count, form, representative_entry, total);
    }
  }

  // Entries not backed by the actor's base TESContainer are items acquired at
  // runtime. Negative deltas represent removed base items and are discarded.
  for (uint32_t i = 0; i < change_count; ++i) {
    const InventoryChangeScratch& change = g_inventory_changes[i];
    if (!change.consumed && change.delta > 0)
      AddInventoryCandidate(candidate_count, change.form, change.entry,
                            change.delta);
  }
  if (g_first_live_sample)
    PluginLog(false, "inventory-safe: candidates=%u", candidate_count);

  g_inventory_discovered_count = 0;
  for (uint32_t i = 0; i < candidate_count; ++i) {
    if (FormId(g_inventory_candidates[i].form) != 0x0000000F)
      ++g_inventory_discovered_count;
  }
  if (g_inventory_discovered_count > dis::kMaxItems)
    g_inventory_truncated = true;
  // Container-change linked lists are not ordered and may be relinked by the
  // engine.  A deterministic form ordering prevents inventory rows and the
  // equipped summary from appearing to rotate on each one-second refresh.
  std::sort(g_inventory_candidates, g_inventory_candidates + candidate_count,
            [](const InventoryCandidateScratch& a,
               const InventoryCandidateScratch& b) {
              const int a_category = InventoryCategory(FormType(a.form));
              const int b_category = InventoryCategory(FormType(b.form));
              if (a_category != b_category) return a_category < b_category;
              return FormId(a.form) < FormId(b.form);
            });
  // Category grouping prevents a very large miscellaneous collection from
  // starving weapons/apparel at the bounded bridge limit.
  for (int category = 1; category <= 6; ++category) {
    for (uint32_t i = 0; i < candidate_count; ++i) {
      const InventoryCandidateScratch& candidate = g_inventory_candidates[i];
      if (InventoryCategory(FormType(candidate.form)) != category) continue;
      AppendInventoryState(state, player, candidate.form, candidate.entry,
                           candidate.count, equipped_left, equipped_right);
    }
  }
  if (g_first_live_sample)
    PluginLog(false, "inventory-safe: snapshot items=%u", state.item_count);
}

bool ApplyStableHandState(dis::Snapshot& state, void* player) {
  const uint32_t raw_left = FormId(EquippedObject(player, true));
  const uint32_t raw_right = FormId(EquippedObject(player, false));

  if (!g_stable_hands_initialized) {
    g_stable_left_hand_form = raw_left;
    g_stable_right_hand_form = raw_right;
    g_stable_hands_initialized = true;
  } else if (raw_left == g_stable_left_hand_form &&
             raw_right == g_stable_right_hand_form) {
    g_candidate_left_hand_form = raw_left;
    g_candidate_right_hand_form = raw_right;
    g_candidate_hand_samples = 0;
  } else {
    if (raw_left == g_candidate_left_hand_form &&
        raw_right == g_candidate_right_hand_form) {
      ++g_candidate_hand_samples;
    } else {
      g_candidate_left_hand_form = raw_left;
      g_candidate_right_hand_form = raw_right;
      g_candidate_hand_samples = 1;
    }
    // Two consecutive post-update samples distinguish a real loadout change
    // from Skyrim's one-frame draw/sheath hand-off. This only delays display
    // by at most one sampler interval and never writes anything to the actor.
    if (g_candidate_hand_samples >= 2) {
      PluginLog(false,
                "equipment-state: hands changed left=%08X->%08X "
                "right=%08X->%08X samples=%u",
                g_stable_left_hand_form, raw_left,
                g_stable_right_hand_form, raw_right,
                g_candidate_hand_samples);
      g_stable_left_hand_form = raw_left;
      g_stable_right_hand_form = raw_right;
      g_candidate_hand_samples = 0;
    }
  }

  bool visible_changed = false;
  for (uint32_t i = 0; i < state.item_count; ++i) {
    dis::ItemState& item = state.items[i];
    const uint32_t old_flags = item.flags;
    const dis::EquipmentSlot old_slot = item.equip_slot;
    item.flags &= ~(dis::kItemLeftHand | dis::kItemRightHand);
    // A weapon/ammo equipped bit is purely a hand projection. Armor keeps
    // the persistent worn bit discovered from ExtraWorn/ExtraWornLeft.
    if (item.form_type == 0x29 || item.form_type == 0x2A) {
      item.flags &= ~dis::kItemEquipped;
      item.equip_slot = dis::EquipmentSlot::kNone;
    }
    if (item.form_id == g_stable_right_hand_form) {
      item.flags |= dis::kItemEquipped | dis::kItemRightHand;
      if (item.form_type != 0x1A)
        item.equip_slot = dis::EquipmentSlot::kRightHand;
    }
    if (item.form_id == g_stable_left_hand_form) {
      item.flags |= dis::kItemEquipped | dis::kItemLeftHand;
      if (item.form_type != 0x1A &&
          item.equip_slot == dis::EquipmentSlot::kNone)
        item.equip_slot = dis::EquipmentSlot::kLeftHand;
    }
    visible_changed |= item.flags != old_flags || item.equip_slot != old_slot;
  }
  return visible_changed;
}

void BumpSectionRevision(uint64_t& revision) {
  // Zero is the pristine/reset value. Avoid publishing it again on the
  // practically unreachable uint64 wrap so equality remains meaningful.
  if (++revision == 0) ++revision;
}

bool InventorySectionMatchesCache(const dis::Snapshot& state) {
  if (state.item_count != g_item_cache_count ||
      std::memcmp(&state.carry, &g_cached_carry, sizeof(state.carry)) != 0 ||
      state.gold != g_cached_gold)
    return false;
  return state.item_count == 0 ||
         std::memcmp(state.items, g_item_cache,
                     static_cast<size_t>(state.item_count) *
                         sizeof(state.items[0])) == 0;
}

void CommitInventorySectionCache(const dis::Snapshot& state) {
  g_item_cache_count = state.item_count;
  if (state.item_count) {
    std::memcpy(g_item_cache, state.items,
                static_cast<size_t>(state.item_count) *
                    sizeof(state.items[0]));
  }
  g_cached_carry = state.carry;
  g_cached_gold = state.gold;
}

uint64_t MixStateSignature(uint64_t signature, uint64_t value) {
  // SplitMix64's avalanche step keeps pointer/form/count permutations from
  // collapsing into the same cheap topology signature. This is change
  // detection only; it is deliberately not a persistent or cryptographic ID.
  value += 0x9E3779B97F4A7C15ull;
  value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
  value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
  value ^= value >> 31;
  return (signature ^ value) * 0x100000001B3ull;
}

uint64_t InventoryTopologySignature(void* player) {
  uint64_t signature = 0x494E56454E544F52ull;  // "INVENTOR"
  if (!player) return signature;
  signature = MixStateSignature(signature, FormId(EquippedObject(player, true)));
  signature = MixStateSignature(signature,
                                FormId(EquippedObject(player, false)));

  void* changes = FindContainerChanges(player);
  void* data = changes
      ? ReadOr<void*>(reinterpret_cast<uint8_t*>(changes) + 0x10, nullptr)
      : nullptr;
  signature = MixStateSignature(signature,
                                reinterpret_cast<uintptr_t>(data));
  uint32_t carry_bits = 0;
  const float carry = data
      ? ReadOr<float>(reinterpret_cast<uint8_t*>(data) + 0x10, 0.0f)
      : 0.0f;
  std::memcpy(&carry_bits, &carry, sizeof(carry_bits));
  signature = MixStateSignature(signature, carry_bits);

  uint32_t change_count = 0;
  uint64_t change_sum = 0;
  uint64_t change_xor = 0;
  void* node = data ? ReadOr<void*>(data, nullptr) : nullptr;
  while (node && change_count < kInventoryScanLimit) {
    if (!RegionAllows(node, 0x10)) break;
    void* entry = ReadOr<void*>(node, nullptr);
    void* next = ReadOr<void*>(reinterpret_cast<uint8_t*>(node) + 8, nullptr);
    if (entry && RegionAllows(entry, 0x18)) {
      void* form = ReadOr<void*>(entry, nullptr);
      const uint32_t form_id = FormId(form);
      const uint32_t delta = static_cast<uint32_t>(ReadOr<int32_t>(
          reinterpret_cast<uint8_t*>(entry) + 0x10, 0));
      bool worn = false;
      ChooseEntryExtra(entry, true, &worn);
      uint64_t element = MixStateSignature(0xCBF29CE484222325ull, form_id);
      element = MixStateSignature(element, delta);
      element = MixStateSignature(element, worn ? 1u : 0u);
      // ContainerChanges is a linked list whose order may legitimately change.
      // Commutative accumulators detect actual form/count/worn changes without
      // turning a harmless relink into a 50 ms detailed card rebuild.
      change_sum += element;
      change_xor ^= element;
    }
    ++change_count;
    if (next == node) break;
    node = next;
  }
  signature = MixStateSignature(signature, change_count);
  signature = MixStateSignature(signature, change_sum);
  signature = MixStateSignature(signature, change_xor);

  void* base_form = ReadOr<void*>(
      reinterpret_cast<uint8_t*>(player) + 0x40, nullptr);
  if (base_form != g_inventory_signature_base_form) {
    g_inventory_signature_base_form = base_form;
    uint64_t base_signature = 0x42415345434F4E54ull;  // "BASECONT"
    void* container = AsContainer(base_form);
    void** entries = container
        ? ReadOr<void**>(reinterpret_cast<uint8_t*>(container) + 8, nullptr)
        : nullptr;
    const uint32_t base_count = container
        ? std::min<uint32_t>(ReadOr<uint32_t>(
                                 reinterpret_cast<uint8_t*>(container) + 0x10,
                                 0),
                             kInventoryScanLimit)
        : 0;
    uint64_t base_sum = 0;
    uint64_t base_xor = 0;
    if (entries && RegionAllows(
                       entries,
                       static_cast<size_t>(base_count) * sizeof(void*))) {
      for (uint32_t i = 0; i < base_count; ++i) {
        void* base_entry = ReadOr<void*>(entries + i, nullptr);
        if (!base_entry || !RegionAllows(base_entry, 0x18)) continue;
        uint64_t element = MixStateSignature(
            0xCBF29CE484222325ull, ReadOr<uint32_t>(base_entry, 0));
        element = MixStateSignature(
            element,
            FormId(ReadOr<void*>(reinterpret_cast<uint8_t*>(base_entry) + 8,
                                 nullptr)));
        base_sum += element;
        base_xor ^= element;
      }
    }
    base_signature = MixStateSignature(base_signature, base_count);
    base_signature = MixStateSignature(base_signature, base_sum);
    base_signature = MixStateSignature(base_signature, base_xor);
    g_inventory_base_signature = base_signature;
  }
  signature = MixStateSignature(signature, g_inventory_base_signature);
  return signature;
}

bool ApplyFavoriteFlags(dis::Snapshot& state);

void FillInventoryCached(dis::Snapshot& state, void* player) {
  const uint64_t now = GetTickCount64();
  const bool signature_due =
      !g_inventory_cache_valid || now >= g_next_inventory_signature_ms;
  uint64_t signature = g_inventory_cache_signature;
  if (signature_due) {
    const uint64_t signature_started_us = PerfNowUs();
    signature = InventoryTopologySignature(player);
    RecordPerf(g_perf_inventory_signature, signature_started_us);
    g_next_inventory_signature_ms = now + 500;
  }
  const bool hydrate_metadata =
      g_inventory_metadata_pending && !g_first_live_sample;
  const bool topology_changed =
      signature_due && g_inventory_cache_valid &&
      signature != g_inventory_cache_signature;
  if (!g_inventory_cache_valid || topology_changed || hydrate_metadata) {
    const uint64_t rebuild_started_us = PerfNowUs();
    // FillInventory appends and accumulates gold. Reset only its live prefix
    // and scalars; AppendInventoryState initializes each emitted record.
    state.item_count = 0;
    state.carry = 0.0f;
    state.gold = 0;
    g_inventory_truncated = false;
    FillInventory(state, player);
    RecordPerf(g_perf_inventory_rebuild, rebuild_started_us);
    // Preserve the currently known Skyrim favorites when inventory topology
    // changes independently of the magic/favorites topology.
    ApplyFavoriteFlags(state);
    ApplyStableHandState(state, player);
    const bool section_changed = !InventorySectionMatchesCache(state);
    if (section_changed) BumpSectionRevision(state.inventory_revision);
    CommitInventorySectionCache(state);
    g_inventory_cache_valid = true;
    g_inventory_cache_signature = signature;
    g_inventory_metadata_pending = g_first_live_sample;
    if (topology_changed) {
      PluginLog(false,
                "[SKYRIM-INVENTORY] topology changed; detailed cache rebuilt "
                "signature=%016llX",
                static_cast<unsigned long long>(signature));
    }
    if (state.item_count != g_last_logged_item_count ||
        g_inventory_truncated != g_last_logged_inventory_truncated) {
      uint32_t described_equipment = 0;
      uint32_t missing_equipment_effects = 0;
      for (uint32_t i = 0; i < state.item_count; ++i) {
        const bool equipment = state.items[i].form_type == 0x29 ||
                               state.items[i].form_type == 0x1A;
        if (!equipment) continue;
        if (state.items[i].description[0]) ++described_equipment;
        else ++missing_equipment_effects;
      }
      PluginLog(false,
                "inventory: refreshed visible=%u discovered=%u truncated=%u "
                "equipment_effects=%u missing=%u carry=%.1f gold=%d",
                state.item_count, g_inventory_discovered_count,
                g_inventory_truncated ? 1u : 0u, described_equipment,
                missing_equipment_effects, state.carry, state.gold);
      g_last_logged_item_count = state.item_count;
      g_last_logged_inventory_truncated = g_inventory_truncated;
    }
    return;
  }
  // The live snapshot is the cache. Only hand projection can change between
  // topology checks, and it updates the revision/cache iff visible flags do.
  if (ApplyStableHandState(state, player)) {
    BumpSectionRevision(state.inventory_revision);
    CommitInventorySectionCache(state);
  }
}

void ReleaseReferenceLookup(void* reference) {
  if (!reference || !RegionAllows(reinterpret_cast<uint8_t*>(reference) + 0x20,
                                  0x10)) return;
  // LookupReferenceByHandle returns a NiPointer<TESObjectREFR>.  Mirror
  // BSHandleRefObject::DecRefCount exactly so the temporary lookup cannot leak
  // references.  The secondary BSHandleRefObject base starts at +0x20.
  void* handle_object = reinterpret_cast<uint8_t*>(reference) + 0x20;
  auto* ref_count = reinterpret_cast<volatile LONG*>(
      reinterpret_cast<uint8_t*>(handle_object) + 8);
  const LONG remaining = InterlockedDecrement(ref_count);
  if ((remaining & 0x3FF) != 0) return;
  void** table = ReadOr<void**>(handle_object, nullptr);
  void* delete_this = table ? ReadOr<void*>(table + 1, nullptr) : nullptr;
  if (RegionAllows(delete_this, 1, true)) {
    reinterpret_cast<void(*)(void*)>(delete_this)(handle_object);
  }
}

uint32_t ExtraDataType(void* extra) {
  if (!extra || !RegionAllows(extra, 0x10)) return UINT32_MAX;
  void** table = ReadOr<void**>(extra, nullptr);
  void* get_type = table ? ReadOr<void*>(table + 1, nullptr) : nullptr;
  if (!RegionAllows(get_type, 1, true)) return UINT32_MAX;
  return reinterpret_cast<uint32_t(*)(void*)>(get_type)(extra);
}

void* FindMapMarkerData(void* reference) {
  void* extra = reference
      ? ReadOr<void*>(reinterpret_cast<uint8_t*>(reference) + 0x70, nullptr)
      : nullptr;
  for (unsigned visited = 0; extra && visited < 128; ++visited) {
    if (!RegionAllows(extra, 0x18)) break;
    if (ExtraDataType(extra) == 0x2C) {
      void* data = ReadOr<void*>(reinterpret_cast<uint8_t*>(extra) + 0x10,
                                nullptr);
      return RegionAllows(data, 0x18) ? data : nullptr;
    }
    void* next = ReadOr<void*>(reinterpret_cast<uint8_t*>(extra) + 8, nullptr);
    if (next == extra) break;
    extra = next;
  }
  return nullptr;
}

void FillMapMarkers(dis::Snapshot& state, void* player) {
  if (!player || !RegionAllows(reinterpret_cast<uint8_t*>(player) + 0x4F8,
                               0x18)) return;
  auto* handles = ReadOr<uint32_t*>(
      reinterpret_cast<uint8_t*>(player) + 0x4F8, nullptr);
  const uint32_t capacity = ReadOr<uint32_t>(
      reinterpret_cast<uint8_t*>(player) + 0x500, 0);
  const uint32_t size = ReadOr<uint32_t>(
      reinterpret_cast<uint8_t*>(player) + 0x508, 0);
  if (!handles || !capacity || !size || size > capacity || capacity > 4096)
    return;

  using LookupFn = bool(*)(const uint32_t&, void*&);
  auto lookup = reinterpret_cast<LookupFn>(g_base + kLookupReferenceByHandle);
  if (!RegionAllows(reinterpret_cast<void*>(lookup), 1, true)) return;

  const uint32_t scan_count = std::min<uint32_t>(size, 2048);
  const float center_x = g_native_map_anchor_resolved.load(
                             std::memory_order_acquire)
      ? g_native_map_resolved_x.load(std::memory_order_acquire)
      : g_native_map_center_x.load(std::memory_order_acquire);
  const float center_y = g_native_map_anchor_resolved.load(
                             std::memory_order_acquire)
      ? g_native_map_resolved_y.load(std::memory_order_acquire)
      : g_native_map_center_y.load(std::memory_order_acquire);
  PluginLog(false,
            "map-markers: scanning runtime handles=%u center=(%.0f,%.0f)",
            scan_count, center_x, center_y);
  for (uint32_t i = 0; i < scan_count; ++i) {
    const uint32_t handle = ReadOr<uint32_t>(handles + i, 0);
    if (!handle) continue;
    uint32_t lookup_handle = handle;
    void* reference = nullptr;
    const bool found = lookup(lookup_handle, reference);
    if (!found || !reference || !RegionAllows(reference, 0x98)) {
      if (reference) ReleaseReferenceLookup(reference);
      continue;
    }

    void* map_data = FindMapMarkerData(reference);
    if (!map_data) {
      ReleaseReferenceLookup(reference);
      continue;
    }
    const uint8_t game_flags = ReadOr<uint8_t>(
        reinterpret_cast<uint8_t*>(map_data) + 0x10, 0);
    const uint16_t type = ReadOr<uint16_t>(
        reinterpret_cast<uint8_t*>(map_data) + 0x12, 0);
    const float x = ReadOr<float>(reinterpret_cast<uint8_t*>(reference) + 0x54,
                                  0.0f);
    const float y = ReadOr<float>(reinterpret_cast<uint8_t*>(reference) + 0x58,
                                  0.0f);
    const float z = ReadOr<float>(reinterpret_cast<uint8_t*>(reference) + 0x5C,
                                  0.0f);
    // ExtraMapMarkerData uses the engine's raw enum, not map.swf IconTypes,
    // for its special values (raw 64=PlayerSet and 65=YouAreHere). Never copy
    // those values into the UI protocol: active objectives, the authoritative
    // playerMapMarker handle, and the live player arrow are synthesized below
    // with exact Scaleform indices 62..66. This array contributes only normal
    // location types 1..59.
    const bool supported = type >= 1 && type <= 59;
    if (supported && std::isfinite(x) && std::isfinite(y) &&
        std::isfinite(z) && x >= -393216.0f && x <= 393216.0f &&
        y >= -393216.0f && y <= 393216.0f &&
        (game_flags & 1u) != 0) {
      const uint32_t form_id = FormId(reference);
      bool duplicate = false;
      for (uint32_t existing = 0; existing < state.map_marker_count;
           ++existing) {
        if (state.map_markers[existing].form_id == form_id && form_id) {
          duplicate = true;
          break;
        }
      }
      if (!duplicate) {
        int32_t slot = -1;
        if (state.map_marker_count < dis::kMaxMapMarkers) {
          slot = static_cast<int32_t>(state.map_marker_count++);
        } else {
          // currentMapMarkers commonly contains more entries than the bridge
          // payload. Scan the complete bounded handle array and retain the
          // nearest ordinary locations. Quest targets and the player's pin are
          // added by their authoritative paths below and can displace one of
          // these slots. Stopping after the first 128 made the visible set
          // depend on handle insertion order and often produced no nearby
          // markers at all.
          float farthest_distance = -1.0f;
          int32_t farthest_normal = -1;
          for (uint32_t existing = 0;
               existing < state.map_marker_count; ++existing) {
            const dis::MapMarkerState& retained =
                state.map_markers[existing];
            if (retained.type >= 62) continue;
            const float dx = retained.x - center_x;
            const float dy = retained.y - center_y;
            const float distance = dx * dx + dy * dy;
            if (std::isfinite(distance) && distance > farthest_distance) {
              farthest_distance = distance;
              farthest_normal = static_cast<int32_t>(existing);
            }
          }
          const float dx = x - center_x;
          const float dy = y - center_y;
          const float candidate_distance = dx * dx + dy * dy;
          if (farthest_normal >= 0 && std::isfinite(candidate_distance) &&
              candidate_distance < farthest_distance) {
            slot = farthest_normal;
          }
        }
        if (slot >= 0) {
          dis::MapMarkerState& out = state.map_markers[slot];
          out = {};
          out.form_id = form_id;
          out.type = type;
          out.flags = dis::kMapMarkerVisible;
          if (game_flags & 2u) out.flags |= dis::kMapMarkerFastTravel;
          out.x = x;
          out.y = y;
          out.z = z;
          const char* map_name = ReadOr<const char*>(
              reinterpret_cast<uint8_t*>(map_data) + 8, nullptr);
          CopyText(out.name, sizeof(out.name), map_name);
          if (!out.name[0])
            CopyText(out.name, sizeof(out.name), FormName(reference));
        }
      }
    }
    ReleaseReferenceLookup(reference);
  }

  // MapMenu does not place active quest targets in currentMapMarkers. Mirror
  // its 1.5.97 objective builder on the game thread so quest pins exist even
  // when the vanilla map has never been opened. kGetActiveQuestTargets owns
  // the questTargets lock internally; retaining any returned engine pointer
  // beyond this bounded page-entry scan would be unsafe.
  struct RawPointerArray {
    void** data;
    uint32_t capacity;
    uint32_t pad_0c;
    uint32_t size;
    uint32_t pad_14;
  };
  using GetActiveTargetsFn = bool(*)(void*, void**, RawPointerArray**);
  using ResolveQuestTargetFn = uint32_t*(*)(void*, uint32_t*, bool, void*);
  using GetTeleportDataFn = void*(*)(void*);
  auto get_active_targets = reinterpret_cast<GetActiveTargetsFn>(
      g_base + kGetActiveQuestTargets);
  auto resolve_quest_target = reinterpret_cast<ResolveQuestTargetFn>(
      g_base + kResolveQuestTargetHandle);
  auto get_teleport_data = reinterpret_cast<GetTeleportDataFn>(
      g_base + kGetTeleportData);
  uint32_t quest_marker_count = 0;
  if (RegionAllows(reinterpret_cast<void*>(get_active_targets), 1, true) &&
      RegionAllows(reinterpret_cast<void*>(resolve_quest_target), 1, true) &&
      RegionAllows(reinterpret_cast<void*>(get_teleport_data), 1, true) &&
      RegionAllows(reinterpret_cast<uint8_t*>(player) + 0x580, 0x18)) {
    auto* objectives = ReadOr<uint8_t*>(
        reinterpret_cast<uint8_t*>(player) + 0x580, nullptr);
    const uint32_t objective_capacity = ReadOr<uint32_t>(
        reinterpret_cast<uint8_t*>(player) + 0x588, 0);
    const uint32_t objective_count = ReadOr<uint32_t>(
        reinterpret_cast<uint8_t*>(player) + 0x590, 0);
    if (objectives && objective_count <= objective_capacity &&
        objective_capacity <= 2048 &&
        RegionAllows(objectives,
                     static_cast<size_t>(objective_count) * 0x10)) {
      std::array<uint32_t, dis::kMaxMapMarkers> quest_handles = {};
      std::array<int32_t, dis::kMaxMapMarkers> quest_slots = {};
      uint32_t retained_quest_handles = 0;
      for (uint32_t objective_index = 0;
           objective_index < objective_count; ++objective_index) {
        uint8_t* entry = objectives +
            static_cast<size_t>(objective_index) * 0x10;
        void* objective = ReadOr<void*>(entry, nullptr);
        const uint32_t objective_state = ReadOr<uint32_t>(entry + 0x0C, 0);
        if (objective_state != 1 || !objective ||
            !RegionAllows(objective, 0x28))
          continue;
        void* quest = ReadOr<void*>(
            reinterpret_cast<uint8_t*>(objective) + 0x08, nullptr);
        auto* targets = ReadOr<void**>(
            reinterpret_cast<uint8_t*>(objective) + 0x10, nullptr);
        const uint32_t target_count = ReadOr<uint32_t>(
            reinterpret_cast<uint8_t*>(objective) + 0x18, 0);
        if (!quest || !targets || target_count == 0 || target_count > 256 ||
            !RegionAllows(targets,
                          static_cast<size_t>(target_count) * sizeof(void*)))
          continue;

        void* quest_key = quest;
        RawPointerArray* active = nullptr;
        if (!get_active_targets(player, &quest_key, &active) || !active ||
            !RegionAllows(active, sizeof(RawPointerArray)))
          continue;
        const uint32_t active_capacity = ReadOr<uint32_t>(
            reinterpret_cast<uint8_t*>(active) + 0x08, 0);
        const uint32_t active_count = ReadOr<uint32_t>(
            reinterpret_cast<uint8_t*>(active) + 0x10, 0);
        auto* active_data = ReadOr<void**>(active, nullptr);
        if (!active_data || active_count > active_capacity ||
            active_capacity > 256 ||
            !RegionAllows(active_data,
                          static_cast<size_t>(active_count) * sizeof(void*)))
          continue;

        for (uint32_t target_index = 0; target_index < target_count;
             ++target_index) {
          void* target = ReadOr<void*>(targets + target_index, nullptr);
          if (!target || !RegionAllows(target, 0x18)) continue;
          bool is_active = false;
          for (uint32_t active_index = 0; active_index < active_count;
               ++active_index) {
            if (ReadOr<void*>(active_data + active_index, nullptr) == target) {
              is_active = true;
              break;
            }
          }
          if (!is_active) continue;

          uint32_t target_handle = 0;
          resolve_quest_target(target, &target_handle, true, quest);
          if (!target_handle || target_handle == UINT32_MAX) continue;
          int32_t prior_slot = -1;
          for (uint32_t retained = 0; retained < retained_quest_handles;
               ++retained) {
            if (quest_handles[retained] == target_handle) {
              prior_slot = quest_slots[retained];
              break;
            }
          }

          uint32_t lookup_handle = target_handle;
          void* reference = nullptr;
          if (!lookup(lookup_handle, reference) || !reference ||
              !RegionAllows(reference, 0x60)) {
            if (reference) ReleaseReferenceLookup(reference);
            continue;
          }
          const float x = ReadOr<float>(
              reinterpret_cast<uint8_t*>(reference) + 0x54, 0.0f);
          const float y = ReadOr<float>(
              reinterpret_cast<uint8_t*>(reference) + 0x58, 0.0f);
          const float z = ReadOr<float>(
              reinterpret_cast<uint8_t*>(reference) + 0x5C, 0.0f);
          const uint16_t marker_type = get_teleport_data(reference) ? 63 : 62;
          if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
              x < -393216.0f || x > 393216.0f ||
              y < -393216.0f || y > 393216.0f) {
            ReleaseReferenceLookup(reference);
            continue;
          }
          if (prior_slot >= 0 &&
              static_cast<uint32_t>(prior_slot) < state.map_marker_count) {
            // Multiple objectives can normalize to the same destination.
            // Two displayed objectives normalized to the same destination use
            // Skyrim's MultipleQuestTargetMarker (IconTypes index 64).
            state.map_markers[prior_slot].type = 64;
            ReleaseReferenceLookup(reference);
            continue;
          }

          int32_t slot = state.map_marker_count < dis::kMaxMapMarkers
              ? static_cast<int32_t>(state.map_marker_count++)
              : -1;
          if (slot < 0) {
            float farthest_distance = -1.0f;
            for (uint32_t retained = 0;
                 retained < state.map_marker_count; ++retained) {
              const dis::MapMarkerState& candidate =
                  state.map_markers[retained];
              if (candidate.type >= 62) continue;
              const float dx = candidate.x - center_x;
              const float dy = candidate.y - center_y;
              const float distance = dx * dx + dy * dy;
              if (std::isfinite(distance) && distance > farthest_distance) {
                farthest_distance = distance;
                slot = static_cast<int32_t>(retained);
              }
            }
          }
          if (slot >= 0) {
            dis::MapMarkerState& out = state.map_markers[slot];
            out = {};
            out.form_id = FormId(reference);
            out.flags = dis::kMapMarkerVisible;
            out.type = marker_type;
            out.x = x;
            out.y = y;
            out.z = z;
            CopyText(out.name, sizeof(out.name), FormName(reference));
            if (retained_quest_handles < dis::kMaxMapMarkers) {
              quest_handles[retained_quest_handles] = target_handle;
              quest_slots[retained_quest_handles] = slot;
              ++retained_quest_handles;
            }
            ++quest_marker_count;
          }
          ReleaseReferenceLookup(reference);
        }
      }
    }
  }

  // PlayerCharacter::RuntimeData::playerMapMarker is the authoritative
  // user-placed destination pin. It is not guaranteed to appear in
  // currentMapMarkers, and reading it directly avoids instantiating MapMenu or
  // calling its PlaceMarker mutation path.
  if (RegionAllows(reinterpret_cast<uint8_t*>(player) + 0x924,
                   sizeof(uint32_t))) {
    uint32_t pin_handle = ReadOr<uint32_t>(
        reinterpret_cast<uint8_t*>(player) + 0x924, 0);
    void* pin_reference = nullptr;
    if (pin_handle && lookup(pin_handle, pin_reference) && pin_reference &&
        RegionAllows(pin_reference, 0x60)) {
      const uint32_t pin_form = FormId(pin_reference);
      const float pin_x = ReadOr<float>(
          reinterpret_cast<uint8_t*>(pin_reference) + 0x54, 0.0f);
      const float pin_y = ReadOr<float>(
          reinterpret_cast<uint8_t*>(pin_reference) + 0x58, 0.0f);
      const float pin_z = ReadOr<float>(
          reinterpret_cast<uint8_t*>(pin_reference) + 0x5C, 0.0f);
      int32_t existing_pin_slot = -1;
      for (uint32_t i = 0; i < state.map_marker_count; ++i) {
        if (state.map_markers[i].type == 65 &&
            pin_form && state.map_markers[i].form_id == pin_form) {
          existing_pin_slot = static_cast<int32_t>(i);
          break;
        }
      }
      if (std::isfinite(pin_x) && std::isfinite(pin_y) &&
          std::isfinite(pin_z)) {
        int32_t slot = existing_pin_slot;
        if (slot < 0 && state.map_marker_count < dis::kMaxMapMarkers)
          slot = static_cast<int32_t>(state.map_marker_count++);
        if (slot < 0) {
          float farthest_distance = -1.0f;
          for (uint32_t i = 0; i < state.map_marker_count; ++i) {
            const dis::MapMarkerState& retained = state.map_markers[i];
            if (retained.type >= 62) continue;
            const float dx = retained.x - center_x;
            const float dy = retained.y - center_y;
            const float distance = dx * dx + dy * dy;
            if (std::isfinite(distance) && distance > farthest_distance) {
              farthest_distance = distance;
              slot = static_cast<int32_t>(i);
            }
          }
        }
        if (slot >= 0) {
          dis::MapMarkerState& pin = state.map_markers[slot];
          pin = {};
          pin.form_id = pin_form;
          pin.flags = dis::kMapMarkerVisible;
          pin.type = 65;
          pin.x = pin_x;
          pin.y = pin_y;
          pin.z = pin_z;
          CopyText(pin.name, sizeof(pin.name), "Custom destination");
        }
      }
    }
    if (pin_reference) ReleaseReferenceLookup(pin_reference);
  }
  PluginLog(false,
            "map-markers: runtime locations_and_pins=%u active_quest_pins=%u",
            state.map_marker_count, quest_marker_count);
}

void FillMapMarkersCached(dis::Snapshot& state, void* player) {
  // The isolated lower-screen terrain renderer consumes world-space markers
  // from the persistent snapshot. Refresh the bounded runtime-data cache only
  // when map-page entry explicitly requests it; ordinary 100 ms telemetry
  // samples retain the prior section without walking reference handles.
  if (!g_map_marker_refresh_requested.exchange(
          false, std::memory_order_acq_rel))
    return;

  const uint64_t started_us = PerfNowUs();
  state.map_marker_count = 0;
  FillMapMarkers(state, player);
  const uint32_t rebuilt_count = std::min(state.map_marker_count,
                                          dis::kMaxMapMarkers);
  AcquireSRWLockExclusive(&g_map_marker_cache_lock);
  const bool section_changed =
      !g_map_marker_cache_valid.load(std::memory_order_acquire) ||
      rebuilt_count != g_map_marker_cache_count ||
      (rebuilt_count != 0 &&
       std::memcmp(g_map_marker_cache, state.map_markers,
                   static_cast<size_t>(rebuilt_count) *
                       sizeof(dis::MapMarkerState)) != 0);
  for (auto& marker : g_map_marker_cache) marker = {};
  if (rebuilt_count) {
    std::memcpy(g_map_marker_cache, state.map_markers,
                static_cast<size_t>(rebuilt_count) *
                    sizeof(dis::MapMarkerState));
  }
  g_map_marker_cache_count = rebuilt_count;
  ReleaseSRWLockExclusive(&g_map_marker_cache_lock);
  if (section_changed) BumpSectionRevision(state.map_revision);
  g_map_marker_cache_valid.store(true, std::memory_order_release);
  g_last_map_marker_refresh_ms = GetTickCount64();
  RecordPerf(g_perf_map_rebuild, started_us);
  PluginLog(false,
            "map-markers: refreshed on page entry locations=%u elapsed_us=%llu",
            rebuilt_count,
            static_cast<unsigned long long>(PerfNowUs() - started_us));

  PluginLog(false,
            "map-markers: published snapshot revision=%llu changed=%u",
            static_cast<unsigned long long>(state.map_revision),
            section_changed ? 1u : 0u);
}

size_t ReadableRegionBytes(const void* address) {
  if (!address) return 0;
  MEMORY_BASIC_INFORMATION info = {};
  if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
      (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return 0;
  const uintptr_t start = reinterpret_cast<uintptr_t>(address);
  const uintptr_t end = reinterpret_cast<uintptr_t>(info.BaseAddress) +
                        info.RegionSize;
  return end > start ? static_cast<size_t>(end - start) : 0;
}

void* SceneVirtualPointer(void* object, size_t slot) {
  if (!object || !RegionAllows(object, sizeof(void*))) return nullptr;
  void** table = ReadOr<void**>(object, nullptr);
  void* function = table ? ReadOr<void*>(table + slot, nullptr) : nullptr;
  if (!RegionAllows(function, 1, true)) return nullptr;
  return reinterpret_cast<void*(*)(void*)>(function)(object);
}

bool ReadSceneTransform(void* object, SceneTransform* transform) {
  if (!transform || !object ||
      !Read(reinterpret_cast<uint8_t*>(object) + 0x7C, transform)) return false;
  for (float value : transform->rotate)
    if (!std::isfinite(value) || std::abs(value) > 4.0f) return false;
  for (float value : transform->translate)
    if (!std::isfinite(value) || std::abs(value) > 10000000.0f) return false;
  return std::isfinite(transform->scale) &&
         std::abs(transform->scale) >= 0.0001f &&
         std::abs(transform->scale) <= 1000.0f;
}

uint32_t VertexStride(uint64_t descriptor) {
  const uint32_t flags = static_cast<uint32_t>((descriptor >> 44) & 0xFFFFu);
  if ((flags & 0x0001u) == 0) return 0;  // VF_VERTEX
  uint32_t stride = 16;                 // float4 position
  if (flags & 0x0002u) stride += 4;     // UV0, half2
  if (flags & 0x0004u) stride += 4;     // UV1, half2
  if (flags & 0x0008u) {
    stride += 4;                        // packed normal
    if (flags & 0x0010u) stride += 4;   // packed tangent
  }
  if (flags & 0x0020u) stride += 4;     // color
  if (flags & 0x0040u) stride += 12;    // skin weights + indices
  if (flags & 0x0100u) stride += 4;     // eye data
  return stride >= 16 && stride <= 128 ? stride : 0;
}


bool AlreadyMagic(const dis::Snapshot& state, uint32_t id) {
  for (uint32_t i = 0; i < state.magic_count; ++i) if (state.magic[i].form_id == id) return true;
  return false;
}

bool ReuseCachedMagicDescription(uint32_t form_id, char* output,
                                 size_t output_size) {
  if (!form_id || !output || output_size == 0) return false;
  for (uint32_t i = 0; i < g_magic_cache_count; ++i) {
    if (g_magic_cache[i].form_id == form_id &&
        g_magic_cache[i].description[0]) {
      CopyText(output, output_size, g_magic_cache[i].description);
      return output[0] != 0;
    }
  }
  return false;
}

uint32_t MagicPreviewFormId(void* form) {
  if (!form) return 0;
  const uint8_t type = FormType(form);
  if (type != 0x16 && type != 0x77) return 0;

  // SpellItem and TESShout both implement BGSMenuDisplayObject. Key the request
  // by that owning form because Inventory3DManager::UpdateMagic3D must receive
  // the SpellItem/Shout; passing the extracted BGSArtObject to UpdateItem3D
  // bypasses Skyrim's magic effect controller/material setup and produces the
  // uniformly purple previews.
  //
  // Do not RTTI-cast the referenced BGSArtObject while enumerating the player's
  // spell lists. The first actor update after a save can overlap display-object
  // streaming, and the old validation called Skyrim's dynamic-cast routine on
  // every MDOB pointer during that window. The form and pointer fields are
  // passive records; UpdateMagic3D performs the authoritative validation later
  // on the renderer-owned request path.
  void* menu_object = nullptr;
  if (type == 0x16)
    menu_object = ReadOr<void*>(reinterpret_cast<uint8_t*>(form) + 0xA8,
                                nullptr);
  else
    menu_object = ReadOr<void*>(reinterpret_cast<uint8_t*>(form) + 0x38,
                                nullptr);
  if (!menu_object || !RegionAllows(menu_object, 0x20))
    return 0;
  return FormId(form);
}

bool ReadStableMagicIdentity(void* form, uint8_t* type_out,
                             uint32_t* id_out) {
  if (!form || !type_out || !id_out || !RegionAllows(form, 0x20))
    return false;
  const uint8_t type = FormType(form);
  // This list intentionally represents only the two form classes supported by
  // Skyrim's native Magic/Favorites UI. A settling Actor::addedSpells slot must
  // never be passed to RTTI or a virtual MagicItem routine merely because its
  // address happens to fall in committed memory.
  const size_t required_size = type == 0x16 ? 0xE8 :
                               type == 0x77 ? 0xA8 : 0;
  if (!required_size || !RegionAllows(form, required_size)) return false;
  void** vtable = ReadOr<void**>(form, nullptr);
  void* destructor = vtable ? ReadOr<void*>(vtable, nullptr) : nullptr;
  if (!vtable || !RegionAllows(vtable, sizeof(void*)) ||
      !RegionAllows(destructor, 1, true))
    return false;
  const uint32_t id = FormId(form);
  if (!id) return false;
  *type_out = type;
  *id_out = id;
  return true;
}

const char* StableMagicName(void* form, uint8_t type) {
  // MagicItem embeds TESFullName at +0x30 and TESShout embeds it at +0x20 in
  // the supported 1.5.97 layouts. Reading the BSFixedString directly avoids a
  // dynamic RTTI call while save-load streaming is still settling.
  const size_t name_offset = type == 0x16 ? 0x38 :
                             type == 0x77 ? 0x28 : 0;
  return name_offset
             ? ReadOr<const char*>(reinterpret_cast<uint8_t*>(form) +
                                       name_offset,
                                   nullptr)
             : nullptr;
}

void AddMagic(dis::Snapshot& state, void* form, void* player) {
  if (!form || state.magic_count >= dis::kMaxMagic) return;
  uint8_t form_type = 0;
  uint32_t id = 0;
  if (!ReadStableMagicIdentity(form, &form_type, &id) ||
      AlreadyMagic(state, id))
    return;

  // Mirror the native Magic menu's selectable record boundary. Actor spell
  // lists also contain permanent race/armor abilities, diseases, enchantment
  // helpers, and other hidden effect carriers. Publishing those records made
  // armor-set bonuses such as Nightingale/Deathbrand appear as fake spells and
  // left blank preview rows because they intentionally have no menu artwork.
  // Only ordinary spells and the two player-selectable power classes belong in
  // this list. TESShout is a separate form type and is accepted below.
  uint32_t stable_spell_type = 11;  // TESShout / voice power.
  if (form_type == 0x16) {
    stable_spell_type =
        ReadOr<uint32_t>(reinterpret_cast<uint8_t*>(form) + 0xC8, 0xFFFFFFFFu);
    if (stable_spell_type != 0 && stable_spell_type != 2 &&
        stable_spell_type != 3)
      return;
  }

  dis::MagicState& out = state.magic[state.magic_count++];
  // Rebuild only the emitted prefix of the persistent section.
  out = {};
  out.form_id = id;
  out.form_type = form_type;
  out.preview_form_id = MagicPreviewFormId(form);
  // SpellItem::Data begins at 0xC0 in Skyrim SE 1.5.97. These are passive
  // record fields, so reading them cannot open a menu or mutate player state.
  if (out.form_type == 0x16) {
    out.spell_type = stable_spell_type;
    using CostFn = float(*)(void*, void*);
    auto calculate_cost = reinterpret_cast<CostFn>(g_base + kCalculateMagicCost);
    // The first live sample runs at the earliest safe passive-record boundary.
    // Skyrim's calculated-cost and localized-description paths allocate and
    // traverse magic effects which can still be streaming at that instant.
    // Publish the validated topology first; the immediately following sample
    // fills those engine-derived fields after the world is fully live.
    if (!g_first_live_sample && player &&
        RegionAllows(reinterpret_cast<void*>(calculate_cost), 1, true)) {
      const float calculated = calculate_cost(form, player);
      if (std::isfinite(calculated) && calculated >= 0.0f) {
        out.cost = calculated;
        out.flags |= dis::kMagicCostKnown;
      }
    }
    if (!(out.flags & dis::kMagicCostKnown)) {
      const int32_t override_cost =
          ReadOr<int32_t>(reinterpret_cast<uint8_t*>(form) + 0xC0, -1);
      const uint32_t spell_flags =
          ReadOr<uint32_t>(reinterpret_cast<uint8_t*>(form) + 0xC4, 0);
      if ((spell_flags & 1u) && override_cost >= 0) {
        out.cost = static_cast<float>(override_cost);
        out.flags |= dis::kMagicCostKnown;
      }
    }

    // MagicItem::effects is a BSTArray<Effect*> at 0x58. The associated
    // ActorValue on the first school-bearing base effect is the same source
    // Skyrim uses to group spells in its native Magic menu.
    void** effects =
        ReadOr<void**>(reinterpret_cast<uint8_t*>(form) + 0x58, nullptr);
    const uint32_t effect_count = std::min<uint32_t>(
        ReadOr<uint32_t>(reinterpret_cast<uint8_t*>(form) + 0x60, 0), 64);
    if (effects && RegionAllows(effects, static_cast<size_t>(effect_count) * 8)) {
      for (uint32_t i = 0; i < effect_count; ++i) {
        void* effect = ReadOr<void*>(effects + i, nullptr);
        void* base_effect = effect
            ? ReadOr<void*>(reinterpret_cast<uint8_t*>(effect) + 0x10, nullptr)
            : nullptr;
        const int32_t school = base_effect
            ? ReadOr<int32_t>(reinterpret_cast<uint8_t*>(base_effect) + 0x78, -1)
            : -1;
        if (school >= 18 && school <= 22) {
          out.school = school;
          break;
        }
      }
    }
  } else if (out.form_type == 0x77) {
    out.spell_type = stable_spell_type;
  }
  CopyText(out.name, sizeof(out.name), StableMagicName(form, form_type));
  if (!g_first_live_sample &&
      !ReuseCachedMagicDescription(id, out.description,
                                   sizeof(out.description)))
    FillFormDescription(form, out.description, sizeof(out.description));
}

bool ApplyMagicFlags(dis::Snapshot& state, void* player) {
  uint32_t equipped[5] = {};
  for (uint32_t slot = 0; slot < 4; ++slot)
    equipped[slot] = FormId(ReadOr<void*>(
        reinterpret_cast<uint8_t*>(player) + 0x1C0 + slot * sizeof(void*), nullptr));
  equipped[4] = FormId(ReadOr<void*>(reinterpret_cast<uint8_t*>(player) + 0x1E0, nullptr));
  bool visible_changed = false;
  for (uint32_t i = 0; i < state.magic_count; ++i) {
    dis::MagicState& magic = state.magic[i];
    const uint32_t old_flags = magic.flags;
    magic.flags &= ~(dis::kMagicEquipped | dis::kMagicFavorite |
                     dis::kMagicLeftHand | dis::kMagicRightHand |
                     dis::kMagicVoice);
    if (magic.form_id == equipped[0])
      magic.flags |= dis::kMagicEquipped | dis::kMagicLeftHand;
    if (magic.form_id == equipped[1])
      magic.flags |= dis::kMagicEquipped | dis::kMagicRightHand;
    if (magic.form_id == equipped[2] || magic.form_id == equipped[3])
      magic.flags |= dis::kMagicEquipped;
    if (magic.form_id == equipped[4])
      magic.flags |= dis::kMagicEquipped | dis::kMagicVoice;
    for (uint32_t n = 0; n < g_all_favorite_count; ++n) {
      if (magic.form_id == g_all_favorite_ids[n]) {
        magic.flags |= dis::kMagicFavorite;
        break;
      }
    }
    visible_changed |= magic.flags != old_flags;
  }
  return visible_changed;
}

void AddSpellData(dis::Snapshot& state, void* spell_data, void* player) {
  if (!spell_data || !RegionAllows(spell_data, 0x28)) return;
  void** spells = ReadOr<void**>(spell_data, nullptr);
  const uint32_t spell_count = std::min<uint32_t>(
      ReadOr<uint32_t>(reinterpret_cast<uint8_t*>(spell_data) + 0x18, 0), 512);
  if (spells && RegionAllows(spells, static_cast<size_t>(spell_count) * sizeof(void*)))
    for (uint32_t i = 0; i < spell_count; ++i)
      AddMagic(state, ReadOr<void*>(spells + i, nullptr), player);

  void** shouts = ReadOr<void**>(reinterpret_cast<uint8_t*>(spell_data) + 0x10, nullptr);
  const uint32_t shout_count = std::min<uint32_t>(
      ReadOr<uint32_t>(reinterpret_cast<uint8_t*>(spell_data) + 0x20, 0), 256);
  if (shouts && RegionAllows(shouts, static_cast<size_t>(shout_count) * sizeof(void*)))
    for (uint32_t i = 0; i < shout_count; ++i)
      AddMagic(state, ReadOr<void*>(shouts + i, nullptr), player);
}

void RememberFavorite(uint32_t form_id) {
  if (!form_id || g_all_favorite_count >= kMaxAllFavorites) return;
  for (uint32_t i = 0; i < g_all_favorite_count; ++i)
    if (g_all_favorite_ids[i] == form_id) return;
  g_all_favorite_ids[g_all_favorite_count++] = form_id;
}

void FillMagicAndFavorites(dis::Snapshot& state, void* player) {
  g_all_favorite_count = 0;
  uint32_t native_hotkeys[dis::kMaxFavorites] = {};
  // Actor::addedSpells uses the local-single-item optimisation in its high bit.
  uint32_t allocation = ReadOr<uint32_t>(reinterpret_cast<uint8_t*>(player) + 0x188, 0);
  void* storage = ReadOr<void*>(reinterpret_cast<uint8_t*>(player) + 0x190, nullptr);
  uint32_t count = std::min<uint32_t>(ReadOr<uint32_t>(reinterpret_cast<uint8_t*>(player) + 0x198, 0), 256);
  if (g_first_live_sample)
    PluginLog(false,
              "magic-safe: stage=added-spells begin allocation=%08X "
              "count=%u storage=%p",
              allocation, count, storage);
  if (allocation & 0x80000000u) {
    if (count) AddMagic(state, storage, player);
  } else if (storage && RegionAllows(storage, static_cast<size_t>(count) * sizeof(void*))) {
    for (uint32_t i = 0; i < count; ++i)
      AddMagic(state, ReadOr<void*>(reinterpret_cast<uint8_t*>(storage) + i * 8, nullptr), player);
  }
  if (g_first_live_sample)
    PluginLog(false, "magic-safe: stage=added-spells complete known=%u",
              state.magic_count);

  // Actor::VisitSpells also includes base/race spells. Read those passive
  // TESSpellList arrays directly so inherited powers and shouts are not lost.
  if (g_first_live_sample)
    PluginLog(false, "magic-safe: stage=base-race begin");
  void* actor_base = ReadOr<void*>(reinterpret_cast<uint8_t*>(player) + 0x40, nullptr);
  if (actor_base && RegionAllows(actor_base, 0xB0))
    AddSpellData(state, ReadOr<void*>(reinterpret_cast<uint8_t*>(actor_base) + 0xA8, nullptr), player);
  void* race = ReadOr<void*>(reinterpret_cast<uint8_t*>(player) + 0x1F0, nullptr);
  if (race && RegionAllows(race, 0x50))
    AddSpellData(state, ReadOr<void*>(reinterpret_cast<uint8_t*>(race) + 0x48, nullptr), player);
  if (g_first_live_sample)
    PluginLog(false, "magic-safe: stage=base-race complete known=%u",
              state.magic_count);

  if (g_first_live_sample)
    PluginLog(false, "magic-safe: stage=favorites begin");
  void* favorites = ReadOr<void*>(reinterpret_cast<void*>(g_base + kMagicFavoritesPtr), nullptr);
  if (favorites) {
    void** all = ReadOr<void**>(reinterpret_cast<uint8_t*>(favorites) + 0x10, nullptr);
    const uint32_t all_count = std::min<uint32_t>(
        ReadOr<uint32_t>(reinterpret_cast<uint8_t*>(favorites) + 0x20, 0),
        kMaxAllFavorites);
    if (all && RegionAllows(all, static_cast<size_t>(all_count) * sizeof(void*))) {
      for (uint32_t i = 0; i < all_count; ++i) {
        void* form = ReadOr<void*>(all + i, nullptr);
        RememberFavorite(FormId(form));
        const uint8_t type = FormType(form);
        if (type == 0x16 || type == 0x77) AddMagic(state, form, player);
      }
    }
    void** hotkeys = ReadOr<void**>(reinterpret_cast<uint8_t*>(favorites) + 0x28, nullptr);
    uint32_t hotkey_count = std::min<uint32_t>(ReadOr<uint32_t>(reinterpret_cast<uint8_t*>(favorites) + 0x38, 0), dis::kMaxFavorites);
    if (hotkeys && RegionAllows(hotkeys, hotkey_count * sizeof(void*))) {
      for (uint32_t i = 0; i < hotkey_count; ++i) {
        void* form = ReadOr<void*>(hotkeys + i, nullptr);
        native_hotkeys[i] = FormId(form);
        const uint8_t type = FormType(form);
        if (type == 0x16 || type == 0x77) AddMagic(state, form, player);
      }
    }
  }
  if (g_first_live_sample)
    PluginLog(false,
              "magic-safe: stage=favorites complete known=%u favorites=%u",
              state.magic_count, g_all_favorite_count);
  ApplyFavoriteFlags(state);
  ApplyMagicFlags(state, player);
  if (g_first_live_sample)
    PluginLog(false, "magic-safe: stage=flags complete");

  // Seed WEMU's eight-slot hotbar from Skyrim's existing numeric hotkeys once
  // per world. Subsequent UI refreshes only read this array; the sole writer is
  // the explicit kAssignHotbar command below.
  if (!g_hotbar_initialized) {
    if (g_loaded_hotbar_available) {
      std::copy_n(g_loaded_hotbar, dis::kMaxFavorites, g_hotbar);
      std::fill_n(g_loaded_hotbar, dis::kMaxFavorites, 0u);
      g_loaded_hotbar_available = false;
      PluginLog(false, "hotbar: restored save-specific assignments");
    } else {
      std::copy_n(native_hotkeys, dis::kMaxFavorites, g_hotbar);
      PluginLog(false, "hotbar: seeded from Skyrim numeric hotkeys");
    }
    g_hotbar_initialized = true;
  }
  state.favorite_count = dis::kMaxFavorites;
  for (uint32_t slot = 0; slot < dis::kMaxFavorites; ++slot) {
    const uint32_t form_id = g_hotbar[slot];
    bool available = form_id == 0;
    for (uint32_t i = 0; !available && i < state.item_count; ++i)
      available = state.items[i].form_id == form_id && state.items[i].count > 0;
    for (uint32_t i = 0; !available && i < state.magic_count; ++i)
      available = state.magic[i].form_id == form_id;
    if (!available) {
      PluginLog(false,
                "hotbar: cleared unavailable slot=%u form=%08X",
                slot + 1, form_id);
      g_hotbar[slot] = 0;
    }
    state.favorites[slot] = g_hotbar[slot];
  }
}

bool ApplyFavoriteFlags(dis::Snapshot& state) {
  bool visible_changed = false;
  for (uint32_t i = 0; i < state.item_count; ++i) {
    const uint32_t old_flags = state.items[i].flags;
    state.items[i].flags &= ~dis::kItemFavorite;
    for (uint32_t n = 0; n < g_all_favorite_count; ++n) {
      if (state.items[i].form_id == g_all_favorite_ids[n]) {
        state.items[i].flags |= dis::kItemFavorite;
        break;
      }
    }
    visible_changed |= state.items[i].flags != old_flags;
  }
  return visible_changed;
}

bool MagicSectionMatchesCache(const dis::Snapshot& state) {
  if (state.magic_count != g_magic_cache_count ||
      state.favorite_count != g_favorite_cache_count)
    return false;
  if (state.magic_count &&
      std::memcmp(state.magic, g_magic_cache,
                  static_cast<size_t>(state.magic_count) *
                      sizeof(state.magic[0])) != 0)
    return false;
  return state.favorite_count == 0 ||
         std::memcmp(state.favorites, g_favorite_cache,
                     static_cast<size_t>(state.favorite_count) *
                         sizeof(state.favorites[0])) == 0;
}

void CommitMagicSectionCache(const dis::Snapshot& state) {
  g_magic_cache_count = state.magic_count;
  if (state.magic_count) {
    std::memcpy(g_magic_cache, state.magic,
                static_cast<size_t>(state.magic_count) *
                    sizeof(state.magic[0]));
  }
  g_favorite_cache_count = state.favorite_count;
  if (state.favorite_count) {
    std::memcpy(g_favorite_cache, state.favorites,
                static_cast<size_t>(state.favorite_count) *
                    sizeof(state.favorites[0]));
  }
}

void MixFormArraySignature(uint64_t& signature, void** forms,
                           uint32_t count, uint32_t limit) {
  count = std::min(count, limit);
  signature = MixStateSignature(signature,
                                reinterpret_cast<uintptr_t>(forms));
  signature = MixStateSignature(signature, count);
  if (!forms ||
      !RegionAllows(forms, static_cast<size_t>(count) * sizeof(void*)))
    return;
  for (uint32_t i = 0; i < count; ++i)
    signature = MixStateSignature(signature,
                                  FormId(ReadOr<void*>(forms + i, nullptr)));
}

void MixSpellDataSignature(uint64_t& signature, void* spell_data) {
  signature = MixStateSignature(signature,
                                reinterpret_cast<uintptr_t>(spell_data));
  if (!spell_data || !RegionAllows(spell_data, 0x28)) return;
  MixFormArraySignature(
      signature, ReadOr<void**>(spell_data, nullptr),
      ReadOr<uint32_t>(reinterpret_cast<uint8_t*>(spell_data) + 0x18, 0),
      512);
  MixFormArraySignature(
      signature,
      ReadOr<void**>(reinterpret_cast<uint8_t*>(spell_data) + 0x10, nullptr),
      ReadOr<uint32_t>(reinterpret_cast<uint8_t*>(spell_data) + 0x20, 0),
      256);
}

uint64_t MagicTopologySignature(void* player) {
  uint64_t signature = 0x4D41474943544F50ull;  // "MAGICTOP"
  if (!player || !RegionAllows(player, 0x200)) return signature;
  const uint32_t allocation = ReadOr<uint32_t>(
      reinterpret_cast<uint8_t*>(player) + 0x188, 0);
  void* storage = ReadOr<void*>(
      reinterpret_cast<uint8_t*>(player) + 0x190, nullptr);
  const uint32_t count = std::min<uint32_t>(ReadOr<uint32_t>(
      reinterpret_cast<uint8_t*>(player) + 0x198, 0), 256);
  signature = MixStateSignature(signature, allocation);
  signature = MixStateSignature(signature,
                                reinterpret_cast<uintptr_t>(storage));
  signature = MixStateSignature(signature, count);
  if (allocation & 0x80000000u) {
    if (count) signature = MixStateSignature(signature, FormId(storage));
  } else if (storage &&
             RegionAllows(storage, static_cast<size_t>(count) * sizeof(void*))) {
    auto** spells = reinterpret_cast<void**>(storage);
    for (uint32_t i = 0; i < count; ++i)
      signature = MixStateSignature(
          signature, FormId(ReadOr<void*>(spells + i, nullptr)));
  }

  void* actor_base = ReadOr<void*>(
      reinterpret_cast<uint8_t*>(player) + 0x40, nullptr);
  MixSpellDataSignature(
      signature,
      actor_base && RegionAllows(actor_base, 0xB0)
          ? ReadOr<void*>(reinterpret_cast<uint8_t*>(actor_base) + 0xA8,
                          nullptr)
          : nullptr);
  void* race = ReadOr<void*>(
      reinterpret_cast<uint8_t*>(player) + 0x1F0, nullptr);
  MixSpellDataSignature(
      signature,
      race && RegionAllows(race, 0x50)
          ? ReadOr<void*>(reinterpret_cast<uint8_t*>(race) + 0x48, nullptr)
          : nullptr);

  void* favorites = ReadOr<void*>(
      reinterpret_cast<void*>(g_base + kMagicFavoritesPtr), nullptr);
  signature = MixStateSignature(signature,
                                reinterpret_cast<uintptr_t>(favorites));
  if (favorites && RegionAllows(favorites, 0x40)) {
    MixFormArraySignature(
        signature,
        ReadOr<void**>(reinterpret_cast<uint8_t*>(favorites) + 0x10, nullptr),
        ReadOr<uint32_t>(reinterpret_cast<uint8_t*>(favorites) + 0x20, 0),
        kMaxAllFavorites);
    MixFormArraySignature(
        signature,
        ReadOr<void**>(reinterpret_cast<uint8_t*>(favorites) + 0x28, nullptr),
        ReadOr<uint32_t>(reinterpret_cast<uint8_t*>(favorites) + 0x38, 0),
        dis::kMaxFavorites);
  }
  return signature;
}

void FillMagicAndFavoritesCached(dis::Snapshot& state, void* player) {
  const uint64_t now = GetTickCount64();
  const bool signature_due =
      !g_magic_cache_valid || now >= g_next_magic_signature_ms;
  uint64_t signature = g_magic_cache_signature;
  if (signature_due) {
    const uint64_t signature_started_us = PerfNowUs();
    signature = MagicTopologySignature(player);
    RecordPerf(g_perf_magic_signature, signature_started_us);
    g_next_magic_signature_ms = now + 1000;
  }
  const bool hydrate_metadata =
      g_magic_metadata_pending && !g_first_live_sample;
  const bool topology_changed =
      signature_due && g_magic_cache_valid &&
      signature != g_magic_cache_signature;
  if (!g_magic_cache_valid || topology_changed || hydrate_metadata) {
    const uint64_t rebuild_started_us = PerfNowUs();
    state.magic_count = 0;
    state.favorite_count = 0;
    FillMagicAndFavorites(state, player);
    RecordPerf(g_perf_magic_rebuild, rebuild_started_us);
    // Rebuilding favorites can also change the favorite bit on inventory rows.
    // Keep the two independently revised sections coherent for this submit.
    if (!InventorySectionMatchesCache(state)) {
      BumpSectionRevision(state.inventory_revision);
      CommitInventorySectionCache(state);
    }
    const bool section_changed = !MagicSectionMatchesCache(state);
    if (section_changed) BumpSectionRevision(state.magic_revision);
    CommitMagicSectionCache(state);
    g_magic_cache_valid = true;
    g_magic_cache_signature = signature;
    g_magic_metadata_pending = g_first_live_sample;
    if (topology_changed) {
      PluginLog(false,
                "[SKYRIM-MAGIC] topology changed; detailed cache rebuilt "
                "signature=%016llX",
                static_cast<unsigned long long>(signature));
    }
    if (state.magic_count != g_last_logged_magic_count ||
        state.favorite_count != g_last_logged_favorite_count) {
      uint32_t native_preview_count = 0;
      for (uint32_t i = 0; i < state.magic_count; ++i)
        if (state.magic[i].preview_form_id) ++native_preview_count;
      PluginLog(false,
                "magic: refreshed known=%u favorites=%u native_previews=%u",
                state.magic_count, state.favorite_count,
                native_preview_count);
      g_last_logged_magic_count = state.magic_count;
      g_last_logged_favorite_count = state.favorite_count;
    }
    return;
  }
  // Favorite topology is part of the signature above, so cached samples only
  // need to project Skyrim's independently changing equipped-hand flags.
  if (ApplyMagicFlags(state, player)) {
    BumpSectionRevision(state.magic_revision);
    CommitMagicSectionCache(state);
  }
}

bool ResolveQuestAliasName(void* quest, const char* alias_name,
                           size_t alias_length, uint32_t instance_id,
                           char* output, size_t output_size) {
  if (!quest || !alias_name || !alias_length || !output || output_size < 2 ||
      alias_length >= 128 || !RegionAllows(quest, 0x70))
    return false;
  char wanted[128] = {};
  std::memcpy(wanted, alias_name, alias_length);

  void** aliases = ReadOr<void**>(reinterpret_cast<uint8_t*>(quest) + 0x58,
                                  nullptr);
  const uint32_t alias_count = std::min<uint32_t>(ReadOr<uint32_t>(
      reinterpret_cast<uint8_t*>(quest) + 0x68, 0), 512);
  if (!aliases ||
      !RegionAllows(aliases, static_cast<size_t>(alias_count) * sizeof(void*)))
    return false;

  for (uint32_t index = 0; index < alias_count; ++index) {
    void* alias = ReadOr<void*>(aliases + index, nullptr);
    if (!alias || !RegionAllows(alias, 0x28)) continue;
    const char* name = ReadOr<const char*>(
        reinterpret_cast<uint8_t*>(alias) + 8, nullptr);
    if (!name || _stricmp(name, wanted) != 0) continue;
    const uint32_t alias_id = ReadOr<uint32_t>(
        reinterpret_cast<uint8_t*>(alias) + 0x18, UINT32_MAX);
    // Radiant/story quests persist the player-facing alias name per quest
    // instance. Prefer that exact journal instance instead of resolving the
    // quest's current alias, which may already belong to a newer repeat.
    void** instance_data = ReadOr<void**>(
        reinterpret_cast<uint8_t*>(quest) + 0x38, nullptr);
    const uint32_t instance_count = std::min<uint32_t>(ReadOr<uint32_t>(
        reinterpret_cast<uint8_t*>(quest) + 0x40, 0), 512);
    if (instance_data && RegionAllows(
            instance_data, static_cast<size_t>(instance_count) * sizeof(void*))) {
      for (uint32_t instance_index = 0;
           instance_index < instance_count; ++instance_index) {
        void* instance = ReadOr<void*>(instance_data + instance_index, nullptr);
        if (!instance || !RegionAllows(instance, 0x18) ||
            ReadOr<uint32_t>(instance, UINT32_MAX) != instance_id)
          continue;
        void* strings = ReadOr<void*>(
            reinterpret_cast<uint8_t*>(instance) + 0x08, nullptr);
        const uint32_t string_count = std::min<uint32_t>(ReadOr<uint32_t>(
            reinterpret_cast<uint8_t*>(instance) + 0x10, 0), 512);
        if (!strings || !RegionAllows(
                strings, static_cast<size_t>(string_count) * 8))
          break;
        for (uint32_t string_index = 0; string_index < string_count;
             ++string_index) {
          const auto* string_data =
              reinterpret_cast<const uint8_t*>(strings) + string_index * 8;
          if (ReadOr<uint32_t>(string_data, UINT32_MAX) != alias_id) continue;
          const uint32_t full_name_form =
              ReadOr<uint32_t>(string_data + 4, 0);
          using LookupFormFn = void*(*)(uint32_t);
          auto lookup_form =
              reinterpret_cast<LookupFormFn>(g_base + kLookupForm);
          void* named_form = full_name_form && RegionAllows(
              reinterpret_cast<void*>(lookup_form), 1, true)
                  ? lookup_form(full_name_form) : nullptr;
          const char* resolved = FormName(named_form);
          if ((!resolved || !resolved[0]) && named_form &&
              (FormType(named_form) == 0x3D || FormType(named_form) == 0x3E)) {
            using DisplayNameFn = const char*(*)(void*);
            auto display_name = reinterpret_cast<DisplayNameFn>(
                g_base + kGetReferenceDisplayName);
            if (RegionAllows(reinterpret_cast<void*>(display_name), 1, true))
              resolved = display_name(named_form);
          }
          CopyText(output, output_size, resolved);
          if (output[0]) return true;
        }
        break;
      }
    }

    // Non-instanced quests, and a still-live current radiant instance, use the
    // same reference-handle path as Skyrim's quest UI.
    using CreateHandleFn = uint32_t*(*)(void*, uint32_t*, uint32_t);
    auto create_handle = reinterpret_cast<CreateHandleFn>(
        g_base + kCreateRefHandleByAliasId);
    if (!RegionAllows(reinterpret_cast<void*>(create_handle), 1, true))
      return false;
    uint32_t handle = 0;
    create_handle(quest, &handle, alias_id);
    if (!handle) return false;
    using LookupFn = bool(*)(const uint32_t&, void*&);
    auto lookup = reinterpret_cast<LookupFn>(g_base + kLookupReferenceByHandle);
    if (!RegionAllows(reinterpret_cast<void*>(lookup), 1, true)) return false;
    void* reference = nullptr;
    uint32_t lookup_handle = handle;
    const bool found = lookup(lookup_handle, reference);
    if (!found || !reference) {
      if (reference) ReleaseReferenceLookup(reference);
      return false;
    }
    using DisplayNameFn = const char*(*)(void*);
    auto display_name = reinterpret_cast<DisplayNameFn>(
        g_base + kGetReferenceDisplayName);
    const char* resolved =
        RegionAllows(reinterpret_cast<void*>(display_name), 1, true)
            ? display_name(reference) : nullptr;
    CopyText(output, output_size, resolved);
    ReleaseReferenceLookup(reference);
    return output[0] != 0;
  }
  return false;
}

bool ResolveQuestGlobalValue(void* quest, const char* global_name,
                             size_t global_length, uint32_t instance_id,
                             char* output, size_t output_size) {
  if (!quest || !global_name || !global_length || global_length >= 128 ||
      !output || output_size < 2 || !RegionAllows(quest, 0x228))
    return false;
  char wanted[128] = {};
  std::memcpy(wanted, global_name, global_length);
  auto matches = [&](void* global) {
    const char* editor_id = FormEditorId(global);
    return editor_id && _stricmp(editor_id, wanted) == 0;
  };
  auto write_value = [&](float value) {
    if (!std::isfinite(value)) return false;
    if (std::abs(value - std::round(value)) < 0.001f)
      std::snprintf(output, output_size, "%.0f", value);
    else
      std::snprintf(output, output_size, "%.1f", value);
    return output[0] != 0;
  };

  // Story Manager snapshots global substitutions alongside alias text for
  // each journal instance. Use that frozen value so completed radiant entries
  // do not change when the live global is reused by a later quest instance.
  void** instance_data = ReadOr<void**>(
      reinterpret_cast<uint8_t*>(quest) + 0x38, nullptr);
  const uint32_t instance_count = std::min<uint32_t>(ReadOr<uint32_t>(
      reinterpret_cast<uint8_t*>(quest) + 0x40, 0), 512);
  if (instance_data && RegionAllows(
          instance_data, static_cast<size_t>(instance_count) * sizeof(void*))) {
    for (uint32_t index = 0; index < instance_count; ++index) {
      void* instance = ReadOr<void*>(instance_data + index, nullptr);
      if (!instance || !RegionAllows(instance, 0x38) ||
          ReadOr<uint32_t>(instance, UINT32_MAX) != instance_id)
        continue;
      const uint8_t* values = ReadOr<uint8_t*>(
          reinterpret_cast<uint8_t*>(instance) + 0x20, nullptr);
      const uint32_t value_count = std::min<uint32_t>(ReadOr<uint32_t>(
          reinterpret_cast<uint8_t*>(instance) + 0x28, 0), 512);
      if (!values || !RegionAllows(values,
          static_cast<size_t>(value_count) * 0x10)) break;
      for (uint32_t value_index = 0; value_index < value_count;
           ++value_index) {
        const uint8_t* value_data = values + value_index * 0x10;
        void* global = ReadOr<void*>(value_data, nullptr);
        if (global && matches(global) &&
            write_value(ReadOr<float>(value_data + 8, 0.0f)))
          return true;
      }
      break;
    }
  }

  // Non-instanced quests reference their live QTGL array instead.
  void* globals = ReadOr<void*>(
      reinterpret_cast<uint8_t*>(quest) + 0x220, nullptr);
  void** global_data = globals ? ReadOr<void**>(globals, nullptr) : nullptr;
  const uint32_t global_count = globals ? std::min<uint32_t>(ReadOr<uint32_t>(
      reinterpret_cast<uint8_t*>(globals) + 8, 0), 512) : 0;
  if (!global_data || !RegionAllows(
          global_data, static_cast<size_t>(global_count) * sizeof(void*)))
    return false;
  for (uint32_t index = 0; index < global_count; ++index) {
    void* global = ReadOr<void*>(global_data + index, nullptr);
    if (global && RegionAllows(global, 0x38) && matches(global) &&
        write_value(ReadOr<float>(reinterpret_cast<uint8_t*>(global) + 0x34,
                                  0.0f)))
      return true;
  }
  return false;
}

void AppendCleanText(char* output, size_t output_size, size_t* used,
                     const char* text) {
  if (!output || !used || !text || *used >= output_size - 1) return;
  while (*text && *used < output_size - 1) {
    const char value = *text++;
    // Collapse control whitespace the same way Scaleform's single objective
    // line does, keeping localized punctuation and UTF-8 bytes untouched.
    if (value == '\r' || value == '\n' || value == '\t') {
      if (*used && output[*used - 1] != ' ') output[(*used)++] = ' ';
    } else {
      output[(*used)++] = value;
    }
  }
  output[*used] = 0;
}

void ResolveQuestObjectiveText(void* quest, const char* raw,
                               uint32_t instance_id, char* output,
                               size_t output_size) {
  if (!output || output_size == 0) return;
  output[0] = 0;
  if (!raw) return;
  size_t used = 0;
  const char* cursor = raw;
  while (*cursor && used < output_size - 1) {
    if (*cursor != '<') {
      char one[2] = {*cursor++, 0};
      AppendCleanText(output, output_size, &used, one);
      continue;
    }
    const char* close = std::strchr(cursor, '>');
    if (!close) break;
    const char* token = cursor + 1;
    const char* equals = static_cast<const char*>(
        std::memchr(token, '=', static_cast<size_t>(close - token)));
    // Skyrim also emits Alias.ShortName/Pronoun/Race variants. They all refer
    // to the same journal-instance alias record, and a compact lower-screen
    // objective should display its resolved player-facing name.
    if (equals && static_cast<size_t>(equals - token) >= 5 &&
        _strnicmp(token, "Alias", 5) == 0) {
      const char* value = equals + 1;
      char resolved[128] = {};
      if (ResolveQuestAliasName(quest, value,
                                static_cast<size_t>(close - value),
                                instance_id, resolved, sizeof(resolved))) {
        AppendCleanText(output, output_size, &used, resolved);
      } else {
        // Do not turn an editor alias identifier into fake player-facing copy.
        // A neutral target label is preferable to exposed developer syntax,
        // and the instance lookup above resolves normal journal entries.
        AppendCleanText(output, output_size, &used, "target");
      }
    } else if (equals && static_cast<size_t>(equals - token) >= 6 &&
               _strnicmp(token, "Global", 6) == 0) {
      const char* value = equals + 1;
      char resolved[32] = {};
      if (ResolveQuestGlobalValue(quest, value,
                                  static_cast<size_t>(close - value),
                                  instance_id, resolved, sizeof(resolved)))
        AppendCleanText(output, output_size, &used, resolved);
    }
    // Other angle-bracket sequences are Scaleform/quest formatting markup;
    // omit the tag itself so the lower UI never displays raw control code.
    cursor = close + 1;
  }
  while (used && output[used - 1] == ' ') output[--used] = 0;
}

bool QuestTextContainsDeveloperSyntax(const char* text) {
  if (!text || !text[0]) return true;
  if (text[0] == '$') return true;  // unresolved Scaleform localization key
  if (std::strchr(text, '<') || std::strchr(text, '>') ||
      std::strchr(text, '{') || std::strchr(text, '}'))
    return true;
  for (const char* cursor = text; *cursor; ++cursor) {
    if (*cursor != '%') continue;
    const char next = cursor[1];
    if (next == 's' || next == 'd' || next == 'u' || next == 'f' ||
        (next >= '0' && next <= '9'))
      return true;
  }
  return false;
}

bool FirstSanitizedQuestDiagnostic(uint32_t form_id) {
  for (uint32_t index = 0; index < g_logged_sanitized_quest_count; ++index)
    if (g_logged_sanitized_quests[index] == form_id) return false;
  if (g_logged_sanitized_quest_count <
      std::size(g_logged_sanitized_quests))
    g_logged_sanitized_quests[g_logged_sanitized_quest_count++] = form_id;
  return true;
}

bool CopyQuestJournalLore(void* player, void* quest, uint16_t current_stage,
                          uint32_t instance_id, char* output,
                          size_t output_size) {
  if (!output || output_size == 0) return false;
  output[0] = 0;
  if (!player || !quest || !RegionAllows(player, 0x580)) return false;

  // PlayerCharacter::questLog is an inline BSSimpleList head at +0x570.  Each
  // entry points at the TESQuestStageItem Skyrim actually placed in Journal
  // Menu, so this excludes unused editor stages and preserves the player's
  // real progress.  Pick the newest log-bearing stage at or below the live
  // quest stage; the stage-item index breaks ties when a stage has several
  // journal entries.
  const uint8_t* node = reinterpret_cast<const uint8_t*>(player) + 0x570;
  void* best_item = nullptr;
  uint16_t best_stage = 0;
  int8_t best_item_index = INT8_MIN;
  for (uint32_t visited = 0; node && visited < 4096; ++visited) {
    if (!RegionAllows(node, 0x10)) break;
    void* item = ReadOr<void*>(node, nullptr);
    const uint8_t* next = ReadOr<const uint8_t*>(node + 8, nullptr);
    if (item && RegionAllows(item, 0x28) &&
        ReadOr<void*>(reinterpret_cast<uint8_t*>(item) + 0x18, nullptr) ==
            quest &&
        ReadOr<uint8_t>(reinterpret_cast<uint8_t*>(item) + 0x16, 0) != 0) {
      void* owning_stage = ReadOr<void*>(
          reinterpret_cast<uint8_t*>(item) + 0x20, nullptr);
      const uint16_t stage = owning_stage && RegionAllows(owning_stage, 2)
          ? ReadOr<uint16_t>(owning_stage, UINT16_MAX)
          : UINT16_MAX;
      const int8_t item_index = ReadOr<int8_t>(
          reinterpret_cast<uint8_t*>(item) + 0x15, INT8_MIN);
      const uint32_t localized_id = ReadOr<uint32_t>(
          reinterpret_cast<uint8_t*>(item) + 0x10, 0);
      if (localized_id && stage != UINT16_MAX && stage <= current_stage &&
          (!best_item || stage > best_stage ||
           (stage == best_stage && item_index > best_item_index))) {
        best_item = item;
        best_stage = stage;
        best_item_index = item_index;
      }
    }
    if (!next || next == node) break;
    node = next;
  }
  if (!best_item) return false;

  // TESQuestStageItem stores a BGSLocalizedStringDL rather than a complete
  // TESDescription component.  TESDescription::GetDescription only consumes
  // the component's file offset/localized ID plus the owning form, so provide
  // the exact 0x10 layout Skyrim expects and let the game's own string tables
  // resolve CNAM.  No game asset or localization data is duplicated here.
  struct JournalDescription {
    void* vtable = nullptr;
    uint32_t file_offset = 0;
    uint32_t localized_id = 0;
  } description;
  static_assert(sizeof(JournalDescription) == 0x10,
                "TESDescription-compatible journal layout changed");
  description.localized_id = ReadOr<uint32_t>(
      reinterpret_cast<uint8_t*>(best_item) + 0x10, 0);
  if (!description.localized_id) return false;

  using GetDescriptionFn = void(*)(void*, GameString*, void*, uint32_t);
  auto get_description = reinterpret_cast<GetDescriptionFn>(
      g_base + kGetDescription);
  if (!RegionAllows(reinterpret_cast<void*>(get_description), 1, true))
    return false;
  constexpr uint32_t kCnam = 0x4D414E43u;
  GameString localized = {};
  get_description(&description, &localized, quest, kCnam);
  char raw[1024] = {};
  CopyText(raw, sizeof(raw), localized.data);
  ReleaseGameString(localized);
  if (!raw[0]) return false;
  ResolveQuestObjectiveText(quest, raw, instance_id, output, output_size);
  return output[0] != 0;
}

bool AppendQuestState(dis::Snapshot& state, void* quest, uint16_t quest_flags,
                      void* player,
                      const uint8_t* player_objectives,
                      uint32_t player_objective_count) {
  if (!quest || state.quest_count >= dis::kMaxQuests ||
      !RegionAllows(quest, 0x268) || !player_objectives ||
      !player_objective_count) return false;

  // PlayerCharacter::objectives contains Skyrim's actual journal instances:
  // {BGSQuestObjective*, instanceID, instanceState}. Reading TESQuest's base
  // objective list alone loses radiant substitutions, includes objectives the
  // player has never received, and ignores the per-instance state displayed by
  // Journal Menu.
  // A radiant quest can leave historical objective records for more than one
  // instance in PlayerCharacter::objectives.  Select one instance first, then
  // export only that instance so the detail page never combines two runs of
  // the same quest.  Skyrim's currentInstanceID wins ties; a displayed
  // objective otherwise has priority over completed/failed history.
  const uint32_t engine_instance_id = ReadOr<uint32_t>(
      reinterpret_cast<uint8_t*>(quest) + 0x50, UINT32_MAX);
  uint32_t selected_instance_id = UINT32_MAX;
  int selected_priority = INT_MAX;
  for (uint32_t index = 0; index < player_objective_count; ++index) {
    const uint8_t* instance = player_objectives + index * 0x10;
    void* objective = ReadOr<void*>(instance, nullptr);
    if (!objective || !RegionAllows(objective, 0x28) ||
        ReadOr<void*>(reinterpret_cast<uint8_t*>(objective) + 8, nullptr) !=
            quest)
      continue;
    const uint32_t instance_id = ReadOr<uint32_t>(instance + 8, UINT32_MAX);
    const uint32_t objective_state = ReadOr<uint32_t>(instance + 0x0C, 0);
    if (objective_state < 1 || objective_state > 5) continue;
    int priority = objective_state == 1 ? 0
                   : (objective_state == 3 || objective_state == 5) ? 2
                                                                     : 4;
    if (instance_id == engine_instance_id) --priority;
    if (priority < selected_priority) {
      selected_priority = priority;
      selected_instance_id = instance_id;
    }
  }
  if (selected_instance_id == UINT32_MAX) return false;

  char preferred_objective[192] = {};
  char fallback_objective[192] = {};
  dis::QuestObjectiveState collected[dis::kMaxQuestObjectives] = {};
  uint16_t objective_count = 0;
  uint16_t objective_stored = 0;
  bool has_current_objective = false;
  bool has_completed_objective = false;
  bool has_failed_objective = false;
  uint16_t hidden_objective_count = 0;
  for (uint32_t index = 0; index < player_objective_count; ++index) {
    const uint8_t* instance = player_objectives + index * 0x10;
    void* objective = ReadOr<void*>(instance, nullptr);
    if (!objective || !RegionAllows(objective, 0x28) ||
        ReadOr<void*>(reinterpret_cast<uint8_t*>(objective) + 8, nullptr) !=
            quest)
      continue;
    const uint32_t instance_id = ReadOr<uint32_t>(instance + 8, UINT32_MAX);
    const uint32_t objective_state = ReadOr<uint32_t>(instance + 0x0C, 0);
    if (instance_id != selected_instance_id || objective_state < 1 ||
        objective_state > 5)
      continue;
    // State 2/4 are Skyrim's hidden completed/failed records. Journal Menu
    // only exposes Displayed, CompletedDisplayed, and FailedDisplayed for the
    // selected quest instance. Exporting the hidden history made the lower
    // page appear to be a wall of completed objectives and did not match the
    // game's journal.
    const bool journal_visible = objective_state == 1 ||
                                 objective_state == 3 ||
                                 objective_state == 5;
    if (!journal_visible) {
      ++hidden_objective_count;
      continue;
    }
    char objective_text[192] = {};
    ResolveQuestObjectiveText(
        quest, ReadOr<const char*>(objective, nullptr), instance_id,
        objective_text, sizeof(objective_text));
    if (!objective_text[0]) continue;

    const uint16_t objective_index = ReadOr<uint16_t>(
        reinterpret_cast<uint8_t*>(objective) + 0x1C, 0);
    bool duplicate = false;
    for (uint16_t stored = 0; stored < objective_stored; ++stored) {
      if (collected[stored].index == objective_index) {
        duplicate = true;
        // Preserve the newest state/text if a mod emitted a duplicate record.
        collected[stored].state = static_cast<uint8_t>(objective_state);
        CopyText(collected[stored].text, sizeof(collected[stored].text),
                 objective_text);
        break;
      }
    }
    if (duplicate) continue;
    ++objective_count;
    has_current_objective |= objective_state == 1;
    has_completed_objective |= objective_state == 3;
    has_failed_objective |= objective_state == 5;
    if (!fallback_objective[0]) {
      std::memcpy(fallback_objective, objective_text,
                  sizeof(fallback_objective));
    }
    // Displayed is the live objective. If the quest is finishing, retain the
    // displayed failed/completed objective as the player-facing fallback.
    if (objective_state == 1 ||
        (!preferred_objective[0] &&
         (objective_state == 3 || objective_state == 5))) {
      std::memcpy(preferred_objective, objective_text,
                  sizeof(preferred_objective));
    }
    if (objective_stored < dis::kMaxQuestObjectives) {
      dis::QuestObjectiveState& stored = collected[objective_stored++];
      stored.index = objective_index;
      stored.state = static_cast<uint8_t>(objective_state);
      CopyText(stored.text, sizeof(stored.text), objective_text);
    }
  }
  if (!objective_count) return false;

  std::sort(collected, collected + objective_stored,
            [](const dis::QuestObjectiveState& left,
               const dis::QuestObjectiveState& right) {
              return left.index < right.index;
            });

  const char* raw_name = FormName(quest);
  const char* editor_id = FormEditorId(quest);
  char safe_name[112] = {};
  ResolveQuestObjectiveText(quest, raw_name, selected_instance_id,
                            safe_name, sizeof(safe_name));
  // TESQuest::FULL is normally already localized by the game's data loader.
  // Some mods put the editor ID or an untranslated Scaleform key there. The
  // vanilla journal groups those records under Miscellaneous; never expose
  // that developer-facing identifier as a lower-screen quest title.
  const bool editor_id_leaked = editor_id && editor_id[0] && safe_name[0] &&
      _stricmp(safe_name, editor_id) == 0;
  if (QuestTextContainsDeveloperSyntax(safe_name) || editor_id_leaked) {
    if (FirstSanitizedQuestDiagnostic(FormId(quest)))
      PluginLog(false,
                "quest: sanitized title form=%08X editor=%s raw=%s",
                FormId(quest),
                editor_id && editor_id[0] ? editor_id : "<none>",
                raw_name && raw_name[0] ? raw_name : "<empty>");
    CopyText(safe_name, sizeof(safe_name), "Miscellaneous");
  }

  dis::QuestState& out = state.quests[state.quest_count++];
  // The quest array persists between samples; initialize only the record that
  // this rebuild emits so a shorter/new quest cannot inherit stale fields.
  out = {};
  out.form_id = FormId(quest);
  std::memcpy(out.name, safe_name, sizeof(out.name));
  std::memcpy(out.objective,
              preferred_objective[0] ? preferred_objective : fallback_objective,
              sizeof(out.objective));
  out.stage = ReadOr<uint16_t>(reinterpret_cast<uint8_t*>(quest) + 0x228, 0);
  out.objective_count = objective_count;
  out.objective_stored = objective_stored;
  if (objective_stored)
    std::memcpy(out.objectives, collected,
                static_cast<size_t>(objective_stored) * sizeof(collected[0]));
  if (!CopyQuestJournalLore(player, quest, out.stage, selected_instance_id,
                            out.lore, sizeof(out.lore))) {
    // Many miscellaneous objectives intentionally have no CNAM journal
    // paragraph. Skyrim still presents their resolved objective as the only
    // player-facing description, so preserve that text instead of displaying
    // a false "missing" message or leaking editor data.
    CopyText(out.lore, sizeof(out.lore), out.objective);
  }
  if (quest_flags & (1u << 11)) out.flags |= dis::kQuestActive;
  // TESQuest flags describe the reusable quest template and can retain a
  // finished bit while a new/radiant instance has a live displayed objective.
  // The journal instance is authoritative for the detail-page status.
  if (!has_current_objective) {
    if (has_failed_objective || (quest_flags & (1u << 6)))
      out.flags |= dis::kQuestFailed;
    else if (has_completed_objective || (quest_flags & (1u << 1)))
      out.flags |= dis::kQuestCompleted;
  }
  static uint32_t logged_objective_diagnostics = 0;
  if (logged_objective_diagnostics < 12) {
    PluginLog(false,
              "quest-objectives: form=%08X instance=%u current=%u "
              "completed_visible=%u failed_visible=%u hidden=%u stored=%u",
              out.form_id, selected_instance_id,
              has_current_objective ? 1u : 0u,
              has_completed_objective ? 1u : 0u,
              has_failed_objective ? 1u : 0u, hidden_objective_count,
              objective_stored);
    ++logged_objective_diagnostics;
  }
  return true;
}

bool QuestSectionMatchesCache(const dis::Snapshot& state) {
  if (state.quest_count != g_quest_cache_count)
    return false;
  return state.quest_count == 0 ||
         std::memcmp(state.quests, g_quest_cache,
                     static_cast<size_t>(state.quest_count) *
                         sizeof(state.quests[0])) == 0;
}

void CommitQuestSectionCache(const dis::Snapshot& state) {
  g_quest_cache_count = state.quest_count;
  if (state.quest_count) {
    std::memcpy(g_quest_cache, state.quests,
                static_cast<size_t>(state.quest_count) *
                    sizeof(state.quests[0]));
  }
}

void SetQuestSectionEmpty(dis::Snapshot& state) {
  const bool visible_changed = state.quest_count != 0;
  state.quest_count = 0;
  if (visible_changed) BumpSectionRevision(state.quest_revision);
  g_quest_cache_count = 0;
  g_quest_cache_signature = 0;
  g_quest_cache_valid = true;
}

void FillQuests(dis::Snapshot& state) {
  if (g_quest_cache_valid && g_sequence < g_next_quest_refresh) {
    return;
  }
  const uint64_t started_ms = GetTickCount64();
  // The raw objective list is cheap to fingerprint. Detailed localization and
  // journal-lore resolution are not, so perform those only after the list or a
  // live quest's tracking flags actually change.
  g_next_quest_refresh = g_sequence + 10;  // lightweight signature check, 1 Hz
  void* player = ReadOr<void*>(reinterpret_cast<void*>(g_base + kPlayerPtr),
                               nullptr);
  if (!player || !RegionAllows(player, 0x590)) {
    SetQuestSectionEmpty(state);
    return;
  }
  const uint8_t* entries = ReadOr<uint8_t*>(
      reinterpret_cast<uint8_t*>(player) + 0x580, nullptr);
  const uint32_t count = std::min<uint32_t>(ReadOr<uint32_t>(
      reinterpret_cast<uint8_t*>(player) + 0x588, 0), 2048);
  if (!entries || !count ||
      !RegionAllows(entries, static_cast<size_t>(count) * 0x10)) {
    SetQuestSectionEmpty(state);
    return;
  }

  struct QuestCandidate {
    void* quest = nullptr;
    uint32_t form_id = 0;
    uint16_t flags = 0;
    bool tracked = false;
  };
  QuestCandidate candidates[dis::kMaxQuests] = {};
  uint32_t candidate_count = 0;
  uint64_t signature = 1469598103934665603ull;
  const auto mix_signature = [&signature](uint64_t value) {
    signature ^= value;
    signature *= 1099511628211ull;
  };
  mix_signature(count);
  struct PlayerObjectiveRecord {
    void* objective = nullptr;
    uint32_t instance_id = UINT32_MAX;
    uint32_t state = 0;
  };
  static_assert(sizeof(PlayerObjectiveRecord) == 0x10,
                "Player objective record layout changed");

  // PlayerCharacter::objectives contains historical completed/failed records
  // as well as the currently displayed journal. The former made the old loop
  // call AppendQuestState once for every historical objective. Because those
  // quests were subsequently dropped, they never entered its dedupe set and
  // were expanded repeatedly (1,700+ times in the captured save). Build a
  // unique candidate set first and only from state 1, Skyrim's live displayed
  // objective state.
  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t* instance = entries + i * 0x10;
    // The complete contiguous array was validated above. Copy each potentially
    // unaligned record directly instead of issuing three VirtualQuery-backed
    // ReadOr calls for all 2,048 slots on every signature check.
    PlayerObjectiveRecord record = {};
    std::memcpy(&record, instance, sizeof(record));
    void* objective = record.objective;
    const uint32_t instance_id = record.instance_id;
    const uint32_t objective_state = record.state;
    mix_signature(reinterpret_cast<uintptr_t>(objective));
    mix_signature((static_cast<uint64_t>(instance_id) << 32) |
                  objective_state);
    if (objective_state != 1 || !objective ||
        !RegionAllows(objective, 0x10))
      continue;
    void* quest = ReadOr<void*>(reinterpret_cast<uint8_t*>(objective) + 8,
                                nullptr);
    if (!quest || !RegionAllows(quest, 0x268)) continue;
    const uint32_t form_id = FormId(quest);
    const uint16_t flags = ReadOr<uint16_t>(
        reinterpret_cast<uint8_t*>(quest) + 0xDC, 0);
    mix_signature((static_cast<uint64_t>(form_id) << 16) | flags);
    const uint32_t quest_instance_id = ReadOr<uint32_t>(
        reinterpret_cast<uint8_t*>(quest) + 0x50, UINT32_MAX);
    const uint16_t quest_stage = ReadOr<uint16_t>(
        reinterpret_cast<uint8_t*>(quest) + 0x228, 0);
    mix_signature((static_cast<uint64_t>(quest_instance_id) << 32) |
                  (static_cast<uint64_t>(quest_stage) << 16) | flags);
    if (!form_id || (flags & (1u << 9))) continue;
    const bool tracked = (flags & (1u << 11)) != 0;
    bool duplicate = false;
    for (uint32_t existing = 0; existing < candidate_count; ++existing) {
      if (candidates[existing].form_id == form_id) {
        duplicate = true;
        // Tracking can change while multiple objectives for the same quest
        // remain displayed. Preserve the latest flag value for ordering.
        candidates[existing].flags = flags;
        candidates[existing].tracked = tracked;
        break;
      }
    }
    if (!duplicate && candidate_count < dis::kMaxQuests) {
      QuestCandidate& candidate = candidates[candidate_count++];
      candidate.quest = quest;
      candidate.form_id = form_id;
      candidate.flags = flags;
      candidate.tracked = tracked;
    } else if (!duplicate && tracked) {
      // The bridge can only publish kMaxQuests records. If an unusually large
      // journal fills the candidate set before a later tracked quest appears,
      // replace an untracked tail so explicitly tracked quests always win.
      for (uint32_t replace = candidate_count; replace-- > 0;) {
        if (candidates[replace].tracked) continue;
        candidates[replace].quest = quest;
        candidates[replace].form_id = form_id;
        candidates[replace].flags = flags;
        candidates[replace].tracked = true;
        break;
      }
    }
  }

  if (g_quest_cache_valid && signature == g_quest_cache_signature) {
    return;
  }

  state.quest_count = 0;
  // Tracked live quests first, then the remaining live journal. Each unique
  // form is expanded exactly once, including any form AppendQuestState rejects.
  for (unsigned pass = 0;
       pass < 2 && state.quest_count < dis::kMaxQuests; ++pass) {
    for (uint32_t i = 0;
         i < candidate_count && state.quest_count < dis::kMaxQuests; ++i) {
      if (candidates[i].tracked != (pass == 0)) continue;
      AppendQuestState(state, candidates[i].quest, candidates[i].flags,
                       player, entries, count);
    }
  }

  const bool section_changed = !QuestSectionMatchesCache(state);
  if (section_changed) BumpSectionRevision(state.quest_revision);
  CommitQuestSectionCache(state);
  g_quest_cache_signature = signature;
  g_quest_cache_valid = true;
  const uint64_t elapsed_ms = GetTickCount64() - started_ms;
  PluginLog(false,
            "quests: rebuilt visible=%u candidates=%u player_objectives=%u "
            "elapsed_ms=%llu",
            state.quest_count, candidate_count, count,
            static_cast<unsigned long long>(elapsed_ms));
}

bool PrepareNativeItemPreviewForEquipmentMutation() {
  if (!g_native_item_models_enabled) return true;
  void* manager = ReadOr<void*>(
      reinterpret_cast<void*>(g_base + kInventory3DManagerPtr), nullptr);
  const bool manager_valid = manager && RegionAllows(manager, 0x160);
  const bool native_inventory_owns =
      NativeInventoryMenuOwnsUi3D(GetTickCount64());
  const bool fully_idle =
      g_native_capture.stage == NativeCaptureStage::kIdle &&
      !g_native_capture.attached_model &&
      !g_native_capture.manager_lifecycle_owned &&
      !g_native_capture.readback_pending &&
      !g_native_capture.readback_copy_issued &&
      !g_deferred_ui3d_target.active &&
      !g_native_capture.entry_submitted &&
      !native_inventory_owns &&
      (!manager_valid || NativeInventoryManagerNeutral(manager));
  if (fully_idle) {
    // Keep the queue paused until the character transaction is also ready.
    // The caller clears this while g_equipment_mutation_in_progress still
    // excludes HUD rendering, immediately before invoking ActorEquipManager.
    g_native_capture_equipment_preempt_requested.store(
        true, std::memory_order_release);
    return true;
  }
  if (!g_native_capture_equipment_preempt_requested.exchange(
          true, std::memory_order_acq_rel)) {
    PluginLog(false,
              "[ITEM_LIFECYCLE] equipment command deferred for native item "
              "renderer stage=%u form=%08X readback=%u copy=%u lease=%u",
              static_cast<unsigned>(g_native_capture.stage),
              g_native_capture.request.form_id,
              g_native_capture.readback_pending ? 1u : 0u,
              g_native_capture.readback_copy_issued ? 1u : 0u,
              g_deferred_ui3d_target.active ? 1u : 0u);
  }
  return false;
}

bool PrepareCharacterPreviewForEquipmentMutation() {
  if (!g_native_character_preview_enabled) return true;
  // PlayerCharacter::Update and HUD/Present are normally the same Skyrim
  // thread on this runtime, but do not make clone lifetime depend on that
  // scheduling detail. Serialize detach/release against native UI3D rendering
  // so a weapon switch cannot free a shallow actor clone while it is being
  // culled or submitted.
  if (g_inside_hud_post_display || g_inside_capture_present) {
    PluginLog(false,
              "[CHAR_MUTATION] equipment change deferred inside UI render "
              "phase thread=%lu",
              static_cast<unsigned long>(GetCurrentThreadId()));
    return false;
  }
  if (!CharacterCloneLifecycleQuiescent()) {
    PluginLog(false,
              "[CHAR_MUTATION] equipment change deferred lease=%u owner=%s "
              "readback=%u copy=%u",
              g_deferred_ui3d_target.active ? 1u : 0u,
              DeferredUi3DTargetOwnerName(g_deferred_ui3d_target.owner),
              g_character_render.readback_pending ? 1u : 0u,
              g_character_render.readback_copy_issued ? 1u : 0u);
    return false;
  }
  void* scene_manager = ReadOr<void*>(
      reinterpret_cast<void*>(g_base + kUI3DSceneManagerPtr), nullptr);
  if (!RetireCharacterSceneObjects(scene_manager,
                                   "equipment graph mutation", true)) {
    PluginLog(false,
              "[CHAR_MUTATION] equipment change rejected; active preview "
              "could not be retired safely clone=%p manager=%p",
              g_character_render.active_clone, scene_manager);
    return false;
  }

  // WEMU-originated equipment commands wait for the short event query as an
  // additional conservative boundary. Advance the scene mutation revision,
  // but retain the last completed CPU/proxy frame. A graph transition is not
  // a loss of the already-captured image, and clearing it here caused the
  // visible blank/pop on every weapon switch.
  g_character_render.submitted_scene_pending_retirement = false;
  g_character_reset_revision.fetch_add(1, std::memory_order_acq_rel);
  g_character_render.readback_pending = false;
  g_character_render.readback_copy_issued = false;
  g_character_render.readback_ready = false;
  g_character_render.readback_success = false;
  g_character_render.readback_dispatched_ms = 0;
  g_character_render.readback_world_epoch = 0;
  g_character_render.readback_source_revision = 0;
  g_character_render.readback_appearance_signature = 0;
  g_character_render.readback_reset_revision = 0;
  g_character_inflight_signature.store(0, std::memory_order_release);
  g_character_inflight_world_epoch.store(0, std::memory_order_release);

  g_character_adopted_signature.store(0, std::memory_order_release);
  g_character_render.consecutive_empty_frames = 0;
  g_character_render.empty_render_suspended = false;
  g_character_candidate_signature = 0;
  g_character_candidate_samples = 0;
  g_character_published_signature = 0;
  g_character_next_clone_ms = 0;
  g_character_clone_retry_requested.store(false, std::memory_order_release);
  g_character_render.refresh_request_control_revision = UINT64_MAX;
  g_character_render.refresh_request_width = 0;
  g_character_render.refresh_request_height = 0;
  const uint64_t now = GetTickCount64();
  const uint64_t requested_until =
      now + kCharacterEquipmentAnimationCaptureMs;
  uint64_t observed_until = g_character_live_transition_until_ms.load(
      std::memory_order_acquire);
  while (observed_until < requested_until &&
         !g_character_live_transition_until_ms.compare_exchange_weak(
             observed_until, requested_until, std::memory_order_acq_rel,
             std::memory_order_acquire)) {
  }
  PluginLog(false,
            "[CHAR_MUTATION] preview scene retired before equipment graph "
            "rebuild cached=%u live_until=%llu",
            g_character_render.has_good_frame ? 1u : 0u,
            static_cast<unsigned long long>(requested_until));
  return true;
}

void DeferEquipmentCommand(const dis::Command& command, const char* reason) {
  // Command polling and execution both occur on PlayerCharacter::Update, so a
  // single slot is sufficient: Sample stops polling as soon as this becomes
  // valid and retries it before accepting another bridge command.
  g_deferred_equipment_command = command;
  g_deferred_equipment_command_valid = true;
  PluginLog(false,
            "[CHAR_MUTATION] command deferred type=%u form=%08X reason=%s",
            static_cast<unsigned>(command.type), command.form_id,
            reason ? reason : "renderer transaction busy");
}

void ReportActionSuccess(const dis::Command& command) {
  if (g_report_action_success)
    g_report_action_success(command.type, command.form_id, command.index);
}

bool LiveFormEquipped(void* player, uint32_t form_id) {
  if (!player || !form_id) return false;
  if (FormId(EquippedObject(player, true)) == form_id ||
      FormId(EquippedObject(player, false)) == form_id)
    return true;

  // Spells, powers and shouts are selected on Actor slots which are not
  // returned by GetEquippedObject. Read the same five live slots used by the
  // native favorites path so settlement covers both physical and magic gear.
  for (uint32_t slot = 0; slot < 4; ++slot) {
    void* selected = ReadOr<void*>(
        reinterpret_cast<uint8_t*>(player) + 0x1C0 + slot * sizeof(void*),
        nullptr);
    if (FormId(selected) == form_id) return true;
  }
  if (FormId(ReadOr<void*>(reinterpret_cast<uint8_t*>(player) + 0x1E0,
                           nullptr)) == form_id)
    return true;

  bool worn = false;
  void* entry = FindInventoryEntry(player, form_id);
  if (entry) ChooseEntryExtra(entry, true, &worn);
  return worn;
}

void CancelEquipmentPreviewPreemption(const char* reason) {
  g_equipment_settlement = {};
  g_equipment_settlement_active.store(false, std::memory_order_release);
  g_native_capture_equipment_preempt_requested.store(
      false, std::memory_order_release);
  PluginLog(false,
            "[EQUIPMENT_SETTLE] CANCEL no actor mutation reason=%s",
            reason ? reason : "command rejected");
}

void BeginEquipmentSettlement(const dis::Command& command,
                              bool expected_equipped,
                              bool track_live_equipped_state) {
  g_equipment_settlement = {};
  g_equipment_settlement.command = command;
  g_equipment_settlement.started_ms = GetTickCount64();
  g_equipment_settlement.expected_equipped = expected_equipped;
  g_equipment_settlement.track_live_equipped_state =
      track_live_equipped_state;
  // The item and character renderers remain preempted across the asynchronous
  // ActorEquipManager tail. ServiceEquipmentSettlement is the only normal
  // path which releases this flag after post-update confirmation.
  g_native_capture_equipment_preempt_requested.store(
      true, std::memory_order_release);
  g_equipment_settlement_active.store(true, std::memory_order_release);
  PluginLog(false,
            "[EQUIPMENT_SETTLE] BEGIN type=%u form=%08X expected=%u "
            "tracked=%u",
            static_cast<unsigned>(command.type), command.form_id,
            expected_equipped ? 1u : 0u,
            track_live_equipped_state ? 1u : 0u);
}

void ServiceEquipmentSettlement(void* player) {
  if (!g_equipment_settlement_active.load(std::memory_order_acquire)) return;

  EquipmentSettlementState& state = g_equipment_settlement;
  ++state.post_update_frames;
  const bool actual = state.track_live_equipped_state
                          ? LiveFormEquipped(player, state.command.form_id)
                          : state.expected_equipped;
  if (!state.track_live_equipped_state ||
      actual == state.expected_equipped) {
    ++state.confirmed_frames;
  } else {
    state.confirmed_frames = 0;
  }

  const uint64_t now = GetTickCount64();
  const uint64_t age_ms = now - state.started_ms;
  const bool confirmed = state.post_update_frames >= 2 &&
                         state.confirmed_frames >= 2;
  const bool timed_out = age_ms >= 3000;
  if (!confirmed && !timed_out) return;

  const dis::Command completed = state.command;
  const uint32_t frames = state.post_update_frames;
  const uint32_t matches = state.confirmed_frames;
  const bool expected = state.expected_equipped;
  const bool tracked = state.track_live_equipped_state;
  g_equipment_settlement = {};
  g_equipment_settlement_active.store(false, std::memory_order_release);
  g_native_capture_equipment_preempt_requested.store(
      false, std::memory_order_release);

  // Refresh only after Skyrim's own update has committed the equipment state;
  // this prevents the UI from publishing the transient pre-rebuild snapshot.
  g_inventory_cache_valid = false;
  g_magic_cache_valid = false;
  g_next_update_sample.store(0, std::memory_order_release);
  if (confirmed) {
    PluginLog(false,
              "[EQUIPMENT_SETTLE] CONFIRMED type=%u form=%08X expected=%u "
              "actual=%u frames=%u matches=%u age=%llums",
              static_cast<unsigned>(completed.type), completed.form_id,
              expected ? 1u : 0u, actual ? 1u : 0u, frames, matches,
              static_cast<unsigned long long>(age_ms));
    ReportActionSuccess(completed);
  } else {
    PluginLog(false,
              "[EQUIPMENT_SETTLE] TIMEOUT type=%u form=%08X expected=%u "
              "actual=%u tracked=%u frames=%u age=%llums; previews released",
              static_cast<unsigned>(completed.type), completed.form_id,
              expected ? 1u : 0u, actual ? 1u : 0u, tracked ? 1u : 0u,
              frames, static_cast<unsigned long long>(age_ms));
  }
}

void ExecuteCommand(void* player, const dis::Command& command,
                    bool deferred_retry) {
  PluginLog(false,
            "command: received type=%u form=%08X index=%d epoch=%llu origin=%s",
            static_cast<unsigned>(command.type), command.form_id,
            command.index,
            static_cast<unsigned long long>(command.world_epoch),
            deferred_retry ? "deferred-retry" : "bridge");
  const uint64_t current_epoch =
      g_world_epoch.load(std::memory_order_acquire);
  if (command.version != dis::kBridgeVersion ||
      command.world_epoch != current_epoch) {
    PluginLog(false,
              "command: rejected stale/invalid version=%08X expected=%08X "
              "epoch=%llu current=%llu",
              command.version, dis::kBridgeVersion,
              static_cast<unsigned long long>(command.world_epoch),
              static_cast<unsigned long long>(current_epoch));
    if (command.type == dis::CommandType::kRequestItemPreview)
      CompleteNativePreviewFailure(command.form_id, command.world_epoch,
                                   "stale/invalid command");
    return;
  }
  if (command.type != dis::CommandType::kRequestMapPreview &&
      command.map_world_id != 0) {
    PluginLog(false,
              "command: rejected non-map command with map world=%08X",
              command.map_world_id);
    return;
  }
  if (command.type == dis::CommandType::kRequestMapPreview) {
    const uint32_t current_map_world =
        g_native_map_world_id.load(std::memory_order_acquire);
    if (!command.map_world_id || command.map_world_id != current_map_world) {
      PluginLog(false,
                "[SKYRIM-MAP] rejected stale worldspace command=%08X "
                "current=%08X revision=%llu",
                command.map_world_id, current_map_world,
                static_cast<unsigned long long>(command.control_revision));
      return;
    }
    if (!std::isfinite(command.map_pan_x) ||
        !std::isfinite(command.map_pan_y) ||
        !std::isfinite(command.map_zoom_delta) ||
        std::abs(command.map_pan_x) > 8.0f ||
        std::abs(command.map_pan_y) > 8.0f ||
        std::abs(command.map_zoom_delta) > 8.0f) {
      PluginLog(false,
                "[SKYRIM-MAP] rejected malformed control "
                "pan=(%.4f,%.4f) zoom=%.4f revision=%llu",
                command.map_pan_x, command.map_pan_y,
                command.map_zoom_delta,
                static_cast<unsigned long long>(command.control_revision));
      return;
    }
    const uint64_t now = GetTickCount64();
    const bool refresh_markers =
        (command.flags & dis::kCommandRefreshMapMarkers) != 0;
    const bool reset_camera =
        (command.flags & dis::kCommandResetMapCamera) != 0;
    if (reset_camera) {
      g_native_map_pending_pan_x.store(0.0f, std::memory_order_release);
      g_native_map_pending_pan_y.store(0.0f, std::memory_order_release);
      g_native_map_pending_zoom.store(0.0f, std::memory_order_release);
      g_native_map_view_offset_x.store(0.0f, std::memory_order_release);
      g_native_map_view_offset_y.store(0.0f, std::memory_order_release);
      g_native_map_view_offset_z.store(0.0f, std::memory_order_release);
      g_native_map_view_zoom.store(0.25f, std::memory_order_release);
      g_native_map_view_initialized.store(false, std::memory_order_release);
      PluginLog(false,
                "[SKYRIM-MAP] private camera reset world=%08X revision=%llu",
                command.map_world_id,
                static_cast<unsigned long long>(command.control_revision));
    }
    const bool marker_cache_valid =
        g_map_marker_cache_valid.load(std::memory_order_acquire);
    // Scanning TESObjectREFR handles belongs on PlayerCharacter::Update, not
    // the render hook. Refresh only on explicit map-page entry (or the first
    // request in a fresh world); camera gestures never trigger the scan.
    if (!marker_cache_valid ||
        (refresh_markers &&
         (!g_last_map_marker_refresh_ms ||
          now - g_last_map_marker_refresh_ms >= 2000))) {
      g_map_marker_refresh_requested.store(true, std::memory_order_release);
      PluginLog(false,
                "map-markers: refresh scheduled page_entry=%u cache_valid=%u "
                "age_ms=%llu",
                refresh_markers ? 1u : 0u, marker_cache_valid ? 1u : 0u,
                static_cast<unsigned long long>(
                    g_last_map_marker_refresh_ms
                        ? now - g_last_map_marker_refresh_ms
                        : 0));
    }
    if (!g_native_map_enabled) {
      PluginLog(false,
                "[map-isolated] data request accepted world=%08X "
                "refresh_markers=%u; native camera/render dispatch disabled",
                command.map_world_id, refresh_markers ? 1u : 0u);
      return;
    }
    if (!g_main_render_world_hooked.load(std::memory_order_acquire)) {
      PluginLog(false,
                "map-native: request rejected enabled=1 world_hook=0");
      return;
    }
    AccumulateNativeMapInput(g_native_map_pending_pan_x,
                             command.map_pan_x, 8.0f);
    AccumulateNativeMapInput(g_native_map_pending_pan_y,
                             command.map_pan_y, 8.0f);
    AccumulateNativeMapInput(g_native_map_pending_zoom,
                             command.map_zoom_delta, 8.0f);
    PublishNativeMapControlRevision(command.control_revision);
    const uint64_t active_deadline = g_native_map_requested_until_ms.load(
        std::memory_order_acquire);
    const uint64_t active_request = g_native_map_active_request.load(
        std::memory_order_acquire);
    if (active_request && now < active_deadline) {
      PluginLog(false,
                "[SKYRIM-MAP] request=%llu coalesced active control_revision="
                "%llu pan=(%.4f,%.4f) zoom=%.4f",
                static_cast<unsigned long long>(active_request),
                static_cast<unsigned long long>(command.control_revision),
                command.map_pan_x, command.map_pan_y,
                command.map_zoom_delta);
      return;
    }
    const uint64_t request =
        g_native_map_request_serial.fetch_add(1, std::memory_order_acq_rel) + 1;
    g_native_map_active_request.store(request, std::memory_order_release);
    g_native_map_request_started_ms.store(now, std::memory_order_release);
    g_native_map_lock_misses.store(0, std::memory_order_release);
    g_native_map_request_failure_base.store(
        g_native_map_failure_count.load(std::memory_order_acquire),
        std::memory_order_release);
    g_native_map_last_failure_reason.store("not-dispatched",
                                           std::memory_order_release);
    g_native_map_last_failure_hr.store(S_OK, std::memory_order_release);
    g_native_map_requested_until_ms.store(
        now + kNativeCaptureRetryWindowMs, std::memory_order_release);
    // The map page consumes Skyrim's own rendered world-map pass; it does not
    // draw the legacy CPU marker array. Do not invalidate/rebuild that unused
    // array when a render request arrives.
    PluginLog(false,
              "[SKYRIM-MAP] request=%llu START epoch=%llu world=%08X "
              "center=(%.0f,%.0f,%.0f) independent_topdown=1 "
              "control_revision=%llu input=(%.4f,%.4f,%.4f) deadline=%llu",
              static_cast<unsigned long long>(request),
              static_cast<unsigned long long>(current_epoch),
              command.map_world_id,
              g_native_map_center_x.load(std::memory_order_acquire),
              g_native_map_center_y.load(std::memory_order_acquire),
              g_native_map_center_z.load(std::memory_order_acquire),
              static_cast<unsigned long long>(command.control_revision),
              command.map_pan_x, command.map_pan_y,
              command.map_zoom_delta,
              static_cast<unsigned long long>(
                  now + kNativeCaptureRetryWindowMs));
    return;
  }
  if (!player || command.form_id == 0) {
    PluginLog(false, "command: rejected missing player/form");
    if (command.type == dis::CommandType::kRequestItemPreview)
      CompleteNativePreviewFailure(command.form_id, command.world_epoch,
                                   "missing player/form");
    return;
  }
  if (command.type != dis::CommandType::kActivateItem &&
      command.type != dis::CommandType::kActivateFavorite &&
      command.type != dis::CommandType::kActivateMagic &&
      command.type != dis::CommandType::kSelectQuest &&
      command.type != dis::CommandType::kRequestItemPreview &&
      command.type != dis::CommandType::kAssignHotbar) return;
  if (command.type != dis::CommandType::kRequestItemPreview) {
    const uint64_t action_now = GetTickCount64();
    if (!deferred_retry && g_last_action_command_ms &&
        action_now - g_last_action_command_ms <= 300 &&
        g_last_action_command.type == command.type &&
        g_last_action_command.form_id == command.form_id &&
        g_last_action_command.index == command.index &&
        g_last_action_command.world_epoch == command.world_epoch) {
      PluginLog(false,
                "command: duplicate action suppressed type=%u form=%08X "
                "index=%d age=%llums",
                static_cast<unsigned>(command.type), command.form_id,
                command.index,
                static_cast<unsigned long long>(action_now -
                                                 g_last_action_command_ms));
      return;
    }
    g_last_action_command = command;
    g_last_action_command_ms = action_now;
  }
  using LookupFn = void*(*)(uint32_t);
  auto lookup = reinterpret_cast<LookupFn>(g_base + kLookupForm);
  if (!RegionAllows(reinterpret_cast<void*>(lookup), 1, true)) {
    PluginLog(false, "command: form lookup hook unavailable");
    if (command.type == dis::CommandType::kRequestItemPreview)
      CompleteNativePreviewFailure(command.form_id, command.world_epoch,
                                   "form lookup unavailable");
    return;
  }
  void* form = lookup(command.form_id);
  if (!form) {
    PluginLog(false, "command: form lookup failed form=%08X", command.form_id);
    if (command.type == dis::CommandType::kRequestItemPreview)
      CompleteNativePreviewFailure(command.form_id, command.world_epoch,
                                   "form lookup failed");
    return;
  }

  if (command.type == dis::CommandType::kAssignHotbar) {
    if (command.index < 0 ||
        command.index >= static_cast<int32_t>(dis::kMaxFavorites)) {
      PluginLog(false, "hotbar: rejected invalid slot=%d form=%08X",
                command.index, command.form_id);
      return;
    }
    bool available = false;
    for (uint32_t i = 0; i < g_item_cache_count; ++i) {
      if (g_item_cache[i].form_id == command.form_id &&
          g_item_cache[i].count > 0) {
        available = true;
        break;
      }
    }
    for (uint32_t i = 0; !available && i < g_magic_cache_count; ++i)
      available = g_magic_cache[i].form_id == command.form_id;
    if (!available) {
      PluginLog(false,
                "hotbar: rejected unavailable form=%08X slot=%d",
                command.form_id, command.index + 1);
      return;
    }
    g_hotbar_initialized = true;
    g_hotbar[command.index] = command.form_id;
    g_magic_cache_valid = false;
    PluginLog(false, "hotbar: assigned slot=%d form=%08X type=%u",
              command.index + 1, command.form_id, FormType(form));
    ReportActionSuccess(command);
    return;
  }

  if (command.type == dis::CommandType::kRequestItemPreview) {
    if (!g_native_item_models_enabled) {
      CompleteNativePreviewFailure(command.form_id, command.world_epoch,
                                   "native item rendering disabled");
      return;
    }
    const auto* show_inventory_3d = reinterpret_cast<const uint8_t*>(
        g_base + kShowInventory3DSetting);
    if (!RegionAllows(show_inventory_3d, sizeof(*show_inventory_3d)) ||
        ReadOr<uint8_t>(show_inventory_3d, 0) == 0) {
      CompleteNativePreviewFailure(command.form_id, command.world_epoch,
                                   "bShowInventory3D disabled");
      return;
    }
    const bool magic_preview = IsNativeMagicPreviewForm(form);
    void* bound_object = magic_preview ? nullptr : AsBoundObject(form);
    // Leveled-list records do not own a renderable model and are known to
    // fault inside Skyrim's asynchronous inventory loader.  Concrete items
    // of every category continue through the native path.
    if ((!magic_preview && !bound_object) || FormType(form) == 0x35) {
      PluginLog(false,
                "item-preview: rejected non-renderable form=%08X type=%u",
                command.form_id, FormType(form));
      CompleteNativePreviewFailure(command.form_id, command.world_epoch,
                                   "non-renderable request");
      return;
    }
    if (!InstallItemPreviewPresentHook() || !InstallHudPostDisplayHook() ||
        !InstallInventoryPostDisplayHook()) {
      PluginLog(false,
                "item-preview: renderer not ready; request deferred form=%08X",
                command.form_id);
      CompleteNativePreviewFailure(command.form_id, command.world_epoch,
                                   "native UI render hook unavailable");
      return;
    }
    QueueNativePreview(
        command.form_id, command.world_epoch, 0, 0, true,
        (command.flags & dis::kCommandPreviewPriority) != 0);
    return;
  }

  if (command.type == dis::CommandType::kSelectQuest) {
    if (FormType(form) != 0x4D || !RegionAllows(form, 0x268)) return;
    const uint16_t original_flags = ReadOr<uint16_t>(
        reinterpret_cast<uint8_t*>(form) + 0xDC, 0);
    if ((original_flags & (1u << 0)) == 0 ||
        (original_flags & ((1u << 1) | (1u << 6))) != 0)
      return;

    // TESForm::AddChange(kQuestFlags) is what Skyrim's own quest flag setters
    // use, so the tracked state is serialized instead of becoming a UI-only
    // bit that disappears on the next load.
    void** vtable = ReadOr<void**>(form, nullptr);
    using AddChangeFn = bool(*)(void*, uint32_t);
    auto add_change = vtable
        ? reinterpret_cast<AddChangeFn>(ReadOr<void*>(vtable + 0x0A, nullptr))
        : nullptr;
    if (!RegionAllows(reinterpret_cast<void*>(add_change), 1, true)) {
      PluginLog(false,
                "command: quest tracking rejected; AddChange unavailable "
                "form=%08X",
                command.form_id);
      return;
    }
    const bool enable = (original_flags & (1u << 11)) == 0;
    const uint16_t flags = enable
        ? static_cast<uint16_t>(original_flags | (1u << 11))
        : static_cast<uint16_t>(original_flags & ~(1u << 11));
    if (!WriteValue(reinterpret_cast<uint8_t*>(form) + 0xDC, flags)) return;
    if (!add_change(form, 1u << 1)) {
      // Do not leave a session-only flag behind if Skyrim could not register
      // the quest change for serialization and normal UI notification.
      WriteValue(reinterpret_cast<uint8_t*>(form) + 0xDC, original_flags);
      PluginLog(false,
                "command: quest tracking rolled back; AddChange failed "
                "form=%08X",
                command.form_id);
      return;
    }

    // PlayerCharacter::forceQuestTargetRepath is bit 7 of +0xBD8. Setting it
    // asks the normal compass/quest-target pipeline to rebuild next update.
    uint8_t player_flags = ReadOr<uint8_t>(
        reinterpret_cast<uint8_t*>(player) + 0xBD8, 0);
    player_flags |= 0x80;
    WriteValue(reinterpret_cast<uint8_t*>(player) + 0xBD8, player_flags);
    g_quest_cache_valid = false;
    g_next_update_sample.store(0, std::memory_order_release);
    PluginLog(false, "command: quest tracking toggled form=%08X active=%u",
              command.form_id, (flags & (1u << 11)) ? 1u : 0u);
    return;
  }

  void* manager = ReadOr<void*>(reinterpret_cast<void*>(g_base + kEquipManagerPtr), nullptr);
  if (!manager) return;
  const uint8_t form_type = FormType(form);
  if (command.type == dis::CommandType::kActivateMagic ||
      (command.type == dis::CommandType::kActivateFavorite &&
       (form_type == 0x16 || form_type == 0x77))) {
    // Use Skyrim's dedicated SpellItem path. This is intentionally separate
    // from EquipObject: treating a spell as TESBoundObject was the unsafe path
    // which previously had to be disabled.
    ScopedAtomicMutationGate mutation_gate(
        &g_equipment_mutation_in_progress);
    if (!mutation_gate) {
      PluginLog(false,
                "[CHAR_MUTATION] concurrent magic equipment change ignored "
                "form=%08X",
                command.form_id);
      DeferEquipmentCommand(command, "concurrent magic mutation");
      return;
    }
    {
      ScopedExclusiveSrwLock render_guard(&g_native_render_lock);
      if (!PrepareNativeItemPreviewForEquipmentMutation() ||
          !PrepareCharacterPreviewForEquipmentMutation()) {
        DeferEquipmentCommand(command, "native preview transaction active");
        return;
      }
    }
    // Decide the toggle from the actor's live slots at execution time. A UI
    // snapshot can be one animation frame behind, and must never be used as
    // authority for an equipment mutation.
    bool selected_in_slot[4] = {};
    for (uint32_t slot = 0; slot < 4; ++slot) {
      selected_in_slot[slot] =
          ReadOr<void*>(reinterpret_cast<uint8_t*>(player) + 0x1C0 +
                            slot * sizeof(void*),
                        nullptr) == form;
    }
    const bool selected_voice =
        ReadOr<void*>(reinterpret_cast<uint8_t*>(player) + 0x1E0, nullptr) ==
        form;
    const bool magic_equipped = selected_voice || selected_in_slot[0] ||
                                selected_in_slot[1] || selected_in_slot[2] ||
                                selected_in_slot[3];

    if (form_type == 0x16) {
      const uint32_t spell_type = ReadOr<uint32_t>(
          reinterpret_cast<uint8_t*>(form) + 0xC8, 0);
      if (spell_type == 4) {
        // Ability records are passive once known. Hotbar activation reaches
        // this guard too, so it cannot accidentally feed one to EquipSpell.
        PluginLog(false, "command: passive magic ignored form=%08X",
                  command.form_id);
        CancelEquipmentPreviewPreemption("passive magic");
        return;
      }
      if (magic_equipped) {
        using UnequipSpellFn = void(*)(void*, void*, void*, int32_t);
        auto unequip_spell = reinterpret_cast<UnequipSpellFn>(
            g_base + kUnequipSpell);
        if (!RegionAllows(reinterpret_cast<void*>(unequip_spell), 1, true)) {
          CancelEquipmentPreviewPreemption("UnequipSpell unavailable");
          return;
        }
        BeginEquipmentSettlement(command, false, true);
        const bool uses_voice = spell_type == 2 || spell_type == 3 ||
                                spell_type == 11 || selected_voice;
        if (uses_voice) {
          // Skyrim may file a power under an internal hand slot while exposing
          // it through selectedPower. Its native Favorites path checks all
          // three accepted removal indices for this case.
          unequip_spell(manager, player, form, 2);
          unequip_spell(manager, player, form, 1);
          unequip_spell(manager, player, form, 0);
        } else {
          bool removed = false;
          for (int32_t slot = 0; slot < 4; ++slot) {
            if (!selected_in_slot[slot]) continue;
            unequip_spell(manager, player, form, slot);
            removed = true;
          }
          if (!removed) unequip_spell(manager, player, form, 2);
        }
        PluginLog(false,
                  "command: magic unequipped form=%08X type=%u "
                  "slots=%u%u%u%u voice=%u",
                  command.form_id, form_type,
                  selected_in_slot[0] ? 1u : 0u,
                  selected_in_slot[1] ? 1u : 0u,
                  selected_in_slot[2] ? 1u : 0u,
                  selected_in_slot[3] ? 1u : 0u,
                  selected_voice ? 1u : 0u);
      } else {
        using EquipSpellFn = void(*)(void*, void*, void*, void*);
        auto equip_spell = reinterpret_cast<EquipSpellFn>(
            g_base + kEquipSpell);
        if (!RegionAllows(reinterpret_cast<void*>(equip_spell), 1, true)) {
          CancelEquipmentPreviewPreemption("EquipSpell unavailable");
          return;
        }
        BeginEquipmentSettlement(command, true, true);
        // Null deliberately invokes Skyrim's own slot selector at 0x638C30;
        // it handles occupied hands, powers, dual casting, and modded slots.
        equip_spell(manager, player, form, nullptr);
        PluginLog(false, "command: magic equipped form=%08X type=%u",
                  command.form_id, form_type);
      }
    } else if (form_type == 0x77) {
      if (magic_equipped) {
        using UnequipShoutFn = void(*)(void*, void*, void*);
        auto unequip_shout = reinterpret_cast<UnequipShoutFn>(
            g_base + kUnequipShout);
        if (!RegionAllows(reinterpret_cast<void*>(unequip_shout), 1, true)) {
          CancelEquipmentPreviewPreemption("UnequipShout unavailable");
          return;
        }
        BeginEquipmentSettlement(command, false, true);
        unequip_shout(manager, player, form);
        PluginLog(false, "command: shout unequipped form=%08X",
                  command.form_id);
      } else {
        using EquipShoutFn = void(*)(void*, void*, void*);
        auto equip_shout = reinterpret_cast<EquipShoutFn>(
            g_base + kEquipShout);
        if (!RegionAllows(reinterpret_cast<void*>(equip_shout), 1, true)) {
          CancelEquipmentPreviewPreemption("EquipShout unavailable");
          return;
        }
        BeginEquipmentSettlement(command, true, true);
        equip_shout(manager, player, form);
        PluginLog(false, "command: shout equipped form=%08X",
                  command.form_id);
      }
    } else {
      CancelEquipmentPreviewPreemption("unsupported magic form type");
      return;
    }
    // Cache invalidation and the UI acknowledgement happen only after two
    // authoritative post-update confirmations in ServiceEquipmentSettlement.
    return;
  }
  void* entry = FindInventoryEntry(player, command.form_id);
  void* entry_form = entry ? ReadOr<void*>(entry, nullptr) : form;
  void* bound_object = AsBoundObject(form);
  // Base-container items legitimately have no InventoryEntryData until Skyrim
  // needs per-instance state. ActorEquipManager accepts a null extra-data list
  // for those items, so do not make the bottom-screen action silently fail.
  if (!entry_form || FormId(entry_form) != command.form_id ||
      entry_form != form || !bound_object) {
    if (command.type == dis::CommandType::kActivateFavorite &&
        !g_logged_unsupported_magic_command.exchange(true, std::memory_order_acq_rel))
      PluginLog(false, "command: non-inventory or magic favorite ignored safely");
    return;
  }
  void* left = EquippedObject(player, true);
  void* right = EquippedObject(player, false);
  bool worn = false;
  void* extra = ChooseEntryExtra(entry, true, &worn);
  if (!extra) extra = ChooseEntryExtra(entry, false, nullptr);
  // Every command reaching ActorEquipManager can affect the actor graph or an
  // inventory model it currently owns (weapons/armor/ammo, but also torches,
  // scrolls, consumables, and modded bound objects). Commands are rare; use the
  // same bounded preview transaction for the whole native Equip/Unequip path.
  const bool changes_actor_equipment = true;
  ScopedAtomicMutationGate mutation_gate(
      changes_actor_equipment ? &g_equipment_mutation_in_progress : nullptr);
  if (!mutation_gate) {
    PluginLog(false,
              "[CHAR_MUTATION] concurrent item equipment change ignored "
              "form=%08X",
              command.form_id);
    DeferEquipmentCommand(command, "concurrent item mutation");
    return;
  }
  {
    ScopedExclusiveSrwLock render_guard(
        changes_actor_equipment ? &g_native_render_lock : nullptr);
    if (changes_actor_equipment &&
        (!PrepareNativeItemPreviewForEquipmentMutation() ||
         !PrepareCharacterPreviewForEquipmentMutation())) {
      DeferEquipmentCommand(command, "native preview transaction active");
      return;
    }
  }
  if ((form == left || form == right || worn) &&
      command.type != dis::CommandType::kActivateFavorite) {
    using Fn = bool(*)(void*, void*, void*, void*, int32_t, void*, bool, bool, bool, bool, void*);
    auto unequip = reinterpret_cast<Fn>(g_base + kUnequipItem);
    if (!RegionAllows(reinterpret_cast<void*>(unequip), 1, true)) {
      CancelEquipmentPreviewPreemption("UnequipObject unavailable");
      return;
    }
    BeginEquipmentSettlement(command, false, true);
    const bool accepted = unequip(manager, player, bound_object, extra, 1,
                                  nullptr, true, false, true, false, nullptr);
    if (!accepted) {
      CancelEquipmentPreviewPreemption("UnequipObject rejected command");
      PluginLog(false, "command: item unequip rejected form=%08X",
                command.form_id);
      return;
    }
    PluginLog(false,
              "command: item unequip accepted form=%08X; awaiting actor "
              "settlement",
              command.form_id);
    return;
  }
  using Fn = void(*)(void*, void*, void*, void*, uint32_t, void*, bool, bool, bool, bool);
  auto equip = reinterpret_cast<Fn>(g_base + kEquipItem);
  if (!RegionAllows(reinterpret_cast<void*>(equip), 1, true)) {
    CancelEquipmentPreviewPreemption("EquipObject unavailable");
    return;
  }
  const bool track_live_equipped_state =
      form_type == 0x1A || form_type == 0x1F || form_type == 0x29 ||
      form_type == 0x2A;
  BeginEquipmentSettlement(command, true, track_live_equipped_state);
  equip(manager, player, bound_object, extra, 1, nullptr, true, false, true,
        false);
  PluginLog(false,
            "command: item equip/use dispatched form=%08X tracked=%u; "
            "awaiting actor settlement",
            command.form_id, track_live_equipped_state ? 1u : 0u);
}

uint64_t PreviewHash(uint64_t value, uint64_t add) {
  value ^= add + 0x9E3779B97F4A7C15ull + (value << 6) + (value >> 2);
  return value;
}

void PreviewLogLine(const char* line) {
  if (line && *line) PluginLog(false, "%s", line);
}

void* LookupFormById(uint32_t form_id) {
  if (!form_id) return nullptr;
  using LookupFn = void*(*)(uint32_t);
  auto lookup = reinterpret_cast<LookupFn>(g_base + kLookupForm);
  return RegionAllows(reinterpret_cast<void*>(lookup), 1, true)
             ? lookup(form_id)
             : nullptr;
}

const char* PreviewModelPath(void* model_component) {
  return model_component
             ? ReadOr<const char*>(
                   reinterpret_cast<uint8_t*>(model_component) + 8, nullptr)
             : nullptr;
}

bool PreviewRaceMatches(void* armor_addon, void* race, void* armor_parent) {
  if (!armor_addon || !race) return false;
  void* primary = ReadOr<void*>(
      reinterpret_cast<uint8_t*>(armor_addon) + 0x28, nullptr);
  if (primary == race || (armor_parent && primary == armor_parent)) return true;
  void** additional = ReadOr<void**>(
      reinterpret_cast<uint8_t*>(armor_addon) + 0x150, nullptr);
  uint32_t count = ReadOr<uint32_t>(
      reinterpret_cast<uint8_t*>(armor_addon) + 0x158, 0);
  count = std::min(count, 256u);
  if (!additional || !RegionAllows(additional, count * sizeof(void*)))
    return false;
  for (uint32_t i = 0; i < count; ++i) {
    void* candidate = ReadOr<void*>(additional + i, nullptr);
    if (candidate == race || (armor_parent && candidate == armor_parent))
      return true;
  }
  return false;
}

dis::player_preview::ComponentKind PreviewArmorKind(uint32_t mask,
                                                     bool base_skin) {
  using Kind = dis::player_preview::ComponentKind;
  if (base_skin) return Kind::kBody;
  if (mask & (1u << 9)) return Kind::kShield;
  if (mask & ((1u << 0) | (1u << 1) | (1u << 11) | (1u << 12) |
              (1u << 13)))
    return Kind::kHelmet;
  if (mask & ((1u << 3) | (1u << 4) | (1u << 28) | (1u << 29)))
    return Kind::kGloves;
  if (mask & ((1u << 7) | (1u << 8) | (1u << 23) | (1u << 24)))
    return Kind::kBoots;
  if (mask & ((1u << 2) | (1u << 16) | (1u << 19) | (1u << 22) |
              (1u << 26)))
    return Kind::kArmor;
  return Kind::kAccessory;
}

void CopyPreviewModelPath(dis::player_preview::Component& component,
                          const char* path, bool weight_pair) {
  if (!path || !*path) return;
  CopyText(component.model_path, sizeof(component.model_path), path);
  if (!weight_pair) return;
  const size_t length = std::strlen(component.model_path);
  if (length < 6 || _stricmp(component.model_path + length - 6, "_0.nif") != 0)
    return;
  CopyText(component.model_path_high, sizeof(component.model_path_high),
           component.model_path);
  component.model_path_high[length - 5] = '1';
}

bool AppendPreviewComponent(dis::player_preview::PlayerManifest& manifest,
                            uint32_t form_id, uint32_t slot_mask,
                            dis::player_preview::ComponentKind kind,
                            const char* model_path, bool base_skin,
                            uint8_t weapon_type = 0xFF,
                            bool weight_pair = false) {
  if (!model_path || !*model_path ||
      manifest.component_count >= dis::player_preview::kMaxComponents)
    return false;
  for (uint32_t i = 0; i < manifest.component_count; ++i) {
    const auto& existing = manifest.components[i];
    if (existing.form_id == form_id && existing.kind == kind &&
        _stricmp(existing.model_path, model_path) == 0)
      return false;
  }
  auto& out = manifest.components[manifest.component_count++];
  out.form_id = form_id;
  out.slot_mask = slot_mask;
  out.kind = kind;
  out.base_skin = base_skin;
  out.weapon_type = weapon_type;
  CopyPreviewModelPath(out, model_path, weight_pair);
  PluginLog(false,
            "[SKYRIM-PREVIEW-ASSET] component form=%08X kind=%u slots=%08X "
            "model=\"%s\" high=\"%s\" base_skin=%u",
            form_id, static_cast<unsigned>(kind), slot_mask,
            out.model_path, out.model_path_high, base_skin ? 1u : 0u);
  return true;
}

void AppendPreviewArmor(dis::player_preview::PlayerManifest& manifest,
                        void* armor, void* race, void* armor_parent,
                        uint8_t sex, bool base_skin) {
  if (!armor || FormType(armor) != 0x1A) return;
  const uint32_t form_id = FormId(armor);
  void** addons = ReadOr<void**>(reinterpret_cast<uint8_t*>(armor) + 0x208,
                                 nullptr);
  uint32_t count = ReadOr<uint32_t>(
      reinterpret_cast<uint8_t*>(armor) + 0x210, 0);
  count = std::min(count, 64u);
  bool appended = false;
  for (uint32_t pass = 0; pass < 2 && !appended; ++pass) {
    for (uint32_t i = 0; addons && i < count; ++i) {
      void* addon = ReadOr<void*>(addons + i, nullptr);
      if (!addon || !RegionAllows(addon, 0x178)) continue;
      if (pass == 0 && !PreviewRaceMatches(addon, race, armor_parent)) continue;
      const uint32_t mask = ReadOr<uint32_t>(
          reinterpret_cast<uint8_t*>(addon) + 0x38, 0);
      void* model = reinterpret_cast<uint8_t*>(addon) + 0x50 +
                    static_cast<size_t>(sex ? 1 : 0) * 0x38;
      const char* path = PreviewModelPath(model);
      if (!path || !*path) continue;
      appended |= AppendPreviewComponent(
          manifest, form_id, mask, PreviewArmorKind(mask, base_skin), path,
          base_skin, 0xFF, true);
    }
  }
  if (!appended) {
    const uint32_t mask = ReadOr<uint32_t>(
        reinterpret_cast<uint8_t*>(armor) + 0x1B8, 0);
    void* model = reinterpret_cast<uint8_t*>(armor) + 0xB8 +
                  static_cast<size_t>(sex ? 1 : 0) * 0x38;
    AppendPreviewComponent(manifest, form_id, mask,
                           PreviewArmorKind(mask, base_skin),
                           PreviewModelPath(model), base_skin, 0xFF, true);
  }
}

uint64_t PreviewAppearanceSignature(void* npc, void* race,
                                    const dis::Snapshot& state) {
  uint64_t signature = 0xCBF29CE484222325ull;
  signature = PreviewHash(signature, FormId(npc));
  signature = PreviewHash(signature, FormId(race));
  signature = PreviewHash(signature, state.world_epoch);
  signature = PreviewHash(signature, ReadOr<uint32_t>(
      reinterpret_cast<uint8_t*>(npc) + 0x38, 0));
  uint32_t weight_bits = 0;
  const float weight = ReadOr<float>(reinterpret_cast<uint8_t*>(npc) + 0x1FC,
                                     50.0f);
  std::memcpy(&weight_bits, &weight, sizeof(weight_bits));
  signature = PreviewHash(signature, weight_bits);
  signature = PreviewHash(signature, ReadOr<uint32_t>(
      reinterpret_cast<uint8_t*>(npc) + 0x246, 0));
  void** head_parts = ReadOr<void**>(
      reinterpret_cast<uint8_t*>(npc) + 0x238, nullptr);
  const uint32_t head_count = std::min<uint32_t>(ReadOr<uint8_t>(
      reinterpret_cast<uint8_t*>(npc) + 0x240, 0), 32);
  for (uint32_t i = 0; head_parts && i < head_count; ++i)
    signature = PreviewHash(signature, FormId(ReadOr<void*>(head_parts + i,
                                                             nullptr)));
  for (uint32_t i = 0; i < state.item_count; ++i) {
    if ((state.items[i].flags & dis::kItemEquipped) == 0) continue;
    void* form = LookupFormById(state.items[i].form_id);
    if (FormType(form) == 0x1A)
      signature = PreviewHash(signature, state.items[i].form_id);
  }
  return signature;
}

void* ResolveLivePlayerNpc(void* player) {
  if (!player) return nullptr;
  void* npc = ReadOr<void*>(reinterpret_cast<uint8_t*>(player) + 0x40, nullptr);
  if (npc && RegionAllows(npc, 0x268)) return npc;

  // TESObjectREFR::baseForm is briefly unpublished on some Proton/FEX save
  // loads even after the PlayerCharacter and cell have stabilized. Form 7 is
  // Skyrim's real mutable Player TESNPC (including the save's name, race,
  // FaceGen data, and appearance), not a generic replacement actor. Resolving
  // that same record through the form table keeps preview initialization from
  // silently stalling on the transient reference link.
  void* canonical_player = LookupFormById(0x00000007u);
  return canonical_player && RegionAllows(canonical_player, 0x268)
             ? canonical_player
             : nullptr;
}

void* ResolveLivePlayerRace(void* player, void* npc) {
  // Match Skyrim's Actor::GetRace semantics. During a new game and immediately
  // after RaceSexMenu changes, Actor runtime data owns the authoritative race
  // before TESNPC::race is serialized back onto form 7. Reading only the base
  // record leaves a valid, visible player permanently stuck on "Preparing".
  // Skyrim SE 1.5.97 Actor::ACTOR_RUNTIME_DATA::race is at +0x1F0.
  void* runtime_race = player && RegionAllows(player, 0x1F8)
      ? ReadOr<void*>(reinterpret_cast<uint8_t*>(player) + 0x1F0, nullptr)
      : nullptr;
  if (runtime_race && RegionAllows(runtime_race, 0x4B8) &&
      FormType(runtime_race) == 0x0E)
    return runtime_race;

  void* base_race = npc && RegionAllows(npc, 0x1F0)
      ? ReadOr<void*>(reinterpret_cast<uint8_t*>(npc) + 0x1E8, nullptr)
      : nullptr;
  return base_race && RegionAllows(base_race, 0x4B8) &&
                 FormType(base_race) == 0x0E
             ? base_race
             : nullptr;
}

void UpdateCustomPlayerPreview(const dis::Snapshot& state, void* player,
                               void* resolved_npc) {
  if (!g_custom_character_preview_enabled || !player || !state.in_game) return;
  void* npc = resolved_npc ? resolved_npc : ResolveLivePlayerNpc(player);
  if (!npc) {
    static uint64_t last_npc_failure_ms = 0;
    const uint64_t now = GetTickCount64();
    if (now - last_npc_failure_ms >= 5000) {
      PluginLog(false,
                "[SKYRIM-PREVIEW] waiting for live Player TESNPC player=%p base=%p",
                player, ReadOr<void*>(reinterpret_cast<uint8_t*>(player) + 0x40,
                                      nullptr));
      last_npc_failure_ms = now;
    }
    return;
  }
  void* race = ResolveLivePlayerRace(player, npc);
  if (!race) {
    // If the reference supplied a partially streamed base record, retry the
    // authoritative Player form before deferring this sample.
    void* canonical_player = LookupFormById(0x00000007u);
    if (canonical_player && canonical_player != npc &&
        RegionAllows(canonical_player, 0x268)) {
      void* canonical_race = ResolveLivePlayerRace(player, canonical_player);
      if (canonical_race) {
        npc = canonical_player;
        race = canonical_race;
      }
    }
  }
  if (!race) {
    static uint64_t last_race_failure_ms = 0;
    const uint64_t now = GetTickCount64();
    if (now - last_race_failure_ms >= 5000) {
      PluginLog(false,
                "[SKYRIM-PREVIEW] waiting for Player race npc=%p npc_form=%08X runtime_race=%p base_race=%p",
                npc, FormId(npc),
                ReadOr<void*>(reinterpret_cast<uint8_t*>(player) + 0x1F0,
                              nullptr),
                ReadOr<void*>(reinterpret_cast<uint8_t*>(npc) + 0x1E8,
                              nullptr));
      last_race_failure_ms = now;
    }
    return;
  }
  const uint64_t signature = PreviewAppearanceSignature(npc, race, state);
  if (signature == g_custom_preview_manifest_signature) return;

  dis::player_preview::PlayerManifest manifest{};
  manifest.world_epoch = state.world_epoch;
  manifest.revision = ++g_custom_preview_manifest_revision;
  manifest.npc_form_id = FormId(npc);
  manifest.race_form_id = FormId(race);
  manifest.sex = (ReadOr<uint32_t>(reinterpret_cast<uint8_t*>(npc) + 0x38, 0) &
                  1u) != 0;
  manifest.weight = std::clamp(ReadOr<float>(
      reinterpret_cast<uint8_t*>(npc) + 0x1FC, 50.0f), 0.0f, 100.0f);
  const uint8_t red = ReadOr<uint8_t>(
      reinterpret_cast<uint8_t*>(npc) + 0x246, 255);
  const uint8_t green = ReadOr<uint8_t>(
      reinterpret_cast<uint8_t*>(npc) + 0x247, 255);
  const uint8_t blue = ReadOr<uint8_t>(
      reinterpret_cast<uint8_t*>(npc) + 0x248, 255);
  manifest.body_tint_rgba = static_cast<uint32_t>(red) |
      (static_cast<uint32_t>(green) << 8) |
      (static_cast<uint32_t>(blue) << 16) | 0xFF000000u;
  CopyText(manifest.skeleton_path, sizeof(manifest.skeleton_path),
           PreviewModelPath(reinterpret_cast<uint8_t*>(race) + 0x98 +
                            static_cast<size_t>(manifest.sex) * 0x28));

  void* armor_parent = ReadOr<void*>(
      reinterpret_cast<uint8_t*>(race) + 0x440, nullptr);
  void* skin = ReadOr<void*>(reinterpret_cast<uint8_t*>(npc) + 0x108, nullptr);
  if (!skin)
    skin = ReadOr<void*>(reinterpret_cast<uint8_t*>(race) + 0x58, nullptr);
  AppendPreviewArmor(manifest, skin, race, armor_parent, manifest.sex, true);

  void** head_parts = ReadOr<void**>(
      reinterpret_cast<uint8_t*>(npc) + 0x238, nullptr);
  const uint32_t head_count = std::min<uint32_t>(ReadOr<uint8_t>(
      reinterpret_cast<uint8_t*>(npc) + 0x240, 0), 32);
  for (uint32_t i = 0; head_parts && i < head_count; ++i) {
    void* head = ReadOr<void*>(head_parts + i, nullptr);
    if (!head || !RegionAllows(head, 0x120)) continue;
    const uint32_t type = ReadOr<uint32_t>(
        reinterpret_cast<uint8_t*>(head) + 0x6C, 0);
    using Kind = dis::player_preview::ComponentKind;
    const Kind kind = type == 2 ? Kind::kEyes
                      : (type == 3 || type == 4) ? Kind::kHair
                                                : Kind::kHead;
    AppendPreviewComponent(
        manifest, FormId(head), 0, kind,
        PreviewModelPath(reinterpret_cast<uint8_t*>(head) + 0x30), false);
  }

  for (uint32_t i = 0; i < state.item_count; ++i) {
    const dis::ItemState& item = state.items[i];
    if ((item.flags & dis::kItemEquipped) == 0) continue;
    void* form = LookupFormById(item.form_id);
    if (FormType(form) == 0x1A)
      AppendPreviewArmor(manifest, form, race, armor_parent, manifest.sex,
                         false);
  }
  // The character portrait is armor-only by design. Hand selections remain
  // available to inventory, equipment, and hotbar state, but they are never
  // resolved into preview components and therefore cannot be drawn in the
  // hands, on the back, hip, or any other skeleton attachment.

  if (!manifest.skeleton_path[0] || !manifest.component_count) {
    PluginLog(false,
              "[SKYRIM-PREVIEW] manifest rejected skeleton=\"%s\" components=%u npc=%08X race=%08X",
              manifest.skeleton_path, manifest.component_count,
              manifest.npc_form_id, manifest.race_form_id);
    return;
  }
  g_custom_preview_manifest_signature = signature;
  PluginLog(false,
            "[SKYRIM-PREVIEW] player race=%08X sex=%s weight=%.1f tint=%08X skeleton=\"%s\" npc=%08X components=%u",
            manifest.race_form_id, manifest.sex ? "female" : "male",
            manifest.weight, manifest.body_tint_rgba,
            manifest.skeleton_path, manifest.npc_form_id,
            manifest.component_count);
  dis::player_preview::PublishManifest(manifest);
}

void Sample() {
  if (!g_data_loaded.load(std::memory_order_acquire) ||
      !g_game_ready.load(std::memory_order_acquire)) {
    KeepLiveOrWait("sampling-not-armed", nullptr, nullptr);
    return;
  }
  const uint64_t now = GetTickCount64();
  if (now < g_sample_not_before.load(std::memory_order_acquire)) {
    KeepLiveOrWait("deferred-after-load", nullptr, nullptr);
    return;
  }
  void* player = Player();
  void* cell = player ? ReadOr<void*>(reinterpret_cast<uint8_t*>(player) + 0x60, nullptr) : nullptr;
  if (!player || !RegionAllows(player, 0xBE0) || !cell || !RegionAllows(cell, 0x20)) {
    g_stable_player = nullptr;
    g_stable_cell = nullptr;
    g_stable_world_samples = 0;
    KeepLiveOrWait("player-or-cell-invalid", player, cell);
    return;
  }
  if (!ActorValueRuntimeReady(player)) {
    g_stable_player = nullptr;
    g_stable_cell = nullptr;
    g_stable_world_samples = 0;
    KeepLiveOrWait("actor-values-not-ready", player, cell);
    return;
  }
  if (player != g_stable_player) {
    g_stable_player = player;
    g_stable_cell = cell;
    g_stable_world_samples = 1;
    KeepLiveOrWait("player-changed", player, cell);
    return;
  }
  if (cell != g_stable_cell) {
    // Crossing an exterior-cell boundary is ordinary gameplay, not a load.
    // Validate one follow-up sample but never revoke a live panel for it.
    g_stable_cell = cell;
    g_stable_world_samples = std::max(1u, g_stable_world_samples);
    KeepLiveOrWait("cell-transition", player, cell);
    return;
  }
  if (g_stable_world_samples < kStableWorldSamples) {
    ++g_stable_world_samples;
    KeepLiveOrWait("world-stabilizing", player, cell);
    return;
  }
  if (g_stable_world_samples == kStableWorldSamples) {
    ++g_stable_world_samples;
    PluginLog(false, "lifecycle: player and cell stable; sampling enabled");
  }
  const uint64_t sample_started_us = PerfNowUs();
  if (g_first_live_sample)
    PluginLog(false, "sample: first-live phase=snapshot-initialize begin");
  dis::Snapshot& state = g_sample_snapshot;
  // The four large sections and their revisions persist until their producer
  // observes a real change. ResetLiveCaches cleared this buffer at the current
  // world boundary; this path updates only the small live scalar header.
  state.version = dis::kBridgeVersion;
  state.size = sizeof(dis::Snapshot);
  state.sequence = ++g_sequence;
  state.in_game = 1;
  state.paused = 0;
  state.world_epoch = g_world_epoch.load(std::memory_order_acquire);
  state.loading_reason = dis::LoadingReason::kNone;
  state.health = ActorValue(player, 24, 1); state.max_health = ActorValue(player, 24, 2);
  state.magicka = ActorValue(player, 25, 1); state.max_magicka = ActorValue(player, 25, 2);
  state.stamina = ActorValue(player, 26, 1); state.max_stamina = ActorValue(player, 26, 2);
  state.max_carry = ActorValue(player, 32, 1);
  const float current_x =
      ReadOr<float>(reinterpret_cast<uint8_t*>(player) + 0x54, 0);
  const float current_y =
      ReadOr<float>(reinterpret_cast<uint8_t*>(player) + 0x58, 0);
  const float current_z =
      ReadOr<float>(reinterpret_cast<uint8_t*>(player) + 0x5C, 0);
  state.player_x = current_x;
  state.player_y = current_y;
  state.player_z = current_z;
  state.heading = ReadOr<float>(reinterpret_cast<uint8_t*>(player) + 0x50, 0);
  // TESObjectCELL::worldSpace is the authoritative world-space pointer at
  // +0x120 in Skyrim SE 1.5.97.  PlayerCharacter+0x628 is unrelated state and
  // previously made the map report an invalid/empty world.  Interior cells
  // have no world-space coordinates, so retain the last known Tamriel
  // position; this matches the native world map's entrance-location behavior.
  const uint16_t cell_flags =
      ReadOr<uint16_t>(reinterpret_cast<uint8_t*>(cell) + 0x40, 1);
  const bool exterior_cell = (cell_flags & 1u) == 0;
  void* world_space = exterior_cell
      ? ReadOr<void*>(reinterpret_cast<uint8_t*>(cell) + 0x120, nullptr)
      : nullptr;
  state.world_form_id = FormId(world_space);
  if (exterior_cell && state.world_form_id == 0x0000003Cu &&
      std::isfinite(current_x) && std::isfinite(current_y) &&
      std::isfinite(current_z)) {
    g_last_tamriel_position_valid = true;
    g_last_tamriel_x = current_x;
    g_last_tamriel_y = current_y;
    g_last_tamriel_z = current_z;
  } else if (!exterior_cell && g_last_tamriel_position_valid) {
    state.world_form_id = 0x0000003Cu;
    state.player_x = g_last_tamriel_x;
    state.player_y = g_last_tamriel_y;
    state.player_z = g_last_tamriel_z;
  }
  // The player arrow is an overlay input only. Publishing these POD values
  // never recenters the retained private map view and never touches
  // PlayerCamera, so walking updates the arrow without moving the map camera.
  g_native_map_player_x.store(state.player_x, std::memory_order_release);
  g_native_map_player_y.store(state.player_y, std::memory_order_release);
  g_native_map_player_z.store(state.player_z, std::memory_order_release);
  g_native_map_player_heading.store(state.heading,
                                    std::memory_order_release);
  // Seed the private camera once per world space. It is deliberately not
  // rewritten while the actor walks: after opening the map, only lower-screen
  // map gestures may change its view. Interior samples retain the last valid
  // exterior anchor exactly as the native Journal Map does.
  const uint32_t anchored_world =
      g_native_map_world_id.load(std::memory_order_acquire);
  const bool anchor_initialized =
      g_native_map_center_initialized.load(std::memory_order_acquire);
  if (state.world_form_id != 0 &&
      (!anchor_initialized || anchored_world != state.world_form_id) &&
      std::isfinite(state.player_x) && std::isfinite(state.player_y) &&
      std::isfinite(state.player_z)) {
    // A worldspace transition creates a genuinely new native map coordinate
    // system. Invalidate only the retained POD view; ordinary actor movement
    // in the same world never recenters the bottom map.
    g_native_map_anchor_resolved.store(false, std::memory_order_release);
    g_native_map_view_initialized.store(false, std::memory_order_release);
    g_native_map_pending_pan_x.store(0.0f, std::memory_order_release);
    g_native_map_pending_pan_y.store(0.0f, std::memory_order_release);
    g_native_map_pending_zoom.store(0.0f, std::memory_order_release);
    g_native_map_control_revision.store(0, std::memory_order_release);
    g_native_map_applied_control_revision.store(0,
                                                 std::memory_order_release);
    // Cancel the old worldspace transaction. A readback already submitted to
    // the GPU is allowed to finish, but its recorded world ID now fails the
    // commit check and can never replace the new map frame.
    g_native_map_requested_until_ms.store(0, std::memory_order_release);
    g_native_map_active_request.store(0, std::memory_order_release);
    g_native_map_request_started_ms.store(0, std::memory_order_release);
    g_native_map_center_x.store(state.player_x, std::memory_order_release);
    g_native_map_center_y.store(state.player_y, std::memory_order_release);
    g_native_map_center_z.store(state.player_z, std::memory_order_release);
    g_native_map_world_id.store(state.world_form_id,
                                std::memory_order_release);
    g_native_map_center_initialized.store(true, std::memory_order_release);
    PluginLog(false,
              "map-native: anchored independent camera world=%08X "
              "center=(%.0f,%.0f,%.0f)",
              state.world_form_id, state.player_x, state.player_y,
              state.player_z);
  }
  CopyText(state.location, sizeof(state.location), FormName(cell));

  void* player_npc = ResolveLivePlayerNpc(player);
  const char* player_name = FormName(player_npc);
  CopyText(state.player_name, sizeof(state.player_name),
           player_name && player_name[0] ? player_name : "Player");

  state.level = 0;
  using LevelFn = uint16_t(*)(void*);
  auto level_fn = reinterpret_cast<LevelFn>(g_base + kGetLevel);
  if (RegionAllows(reinterpret_cast<void*>(level_fn), 1, true)) state.level = level_fn(player);
  state.perk_points = ReadOr<uint8_t>(reinterpret_cast<uint8_t*>(player) + 0xB01, 0);
  state.level_xp = 0.0f;
  state.level_xp_max = 0.0f;
  void* skills = ReadOr<void*>(reinterpret_cast<uint8_t*>(player) + 0x9B0, nullptr);
  void* skill_data = skills ? ReadOr<void*>(skills, nullptr) : nullptr;
  if (skill_data) {
    state.level_xp = ReadOr<float>(skill_data, 0);
    state.level_xp_max = ReadOr<float>(reinterpret_cast<uint8_t*>(skill_data) + 4, 0);
  }
  if (g_first_live_sample)
    PluginLog(false, "sample: first-live phase=inventory begin");
  FillInventoryCached(state, player);
  // Consume only the stable, already-cached equipment projection. Record and
  // asset resolution is event-driven by the compact manifest signature and
  // never walks InventoryChanges from the renderer.
  UpdateCustomPlayerPreview(state, player, player_npc);
  state.armor_rating = std::max(0.0f, ActorValue(player, 39, 1));
  state.damage_rating = 0.0f;
  float left_hand_damage = 0.0f;
  float any_equipped_damage = 0.0f;
  for (uint32_t i = 0; i < state.item_count; ++i) {
    const dis::ItemState& item = state.items[i];
    if ((item.flags & dis::kItemEquipped) == 0 ||
        !std::isfinite(item.damage) || item.damage <= 0.0f)
      continue;
    any_equipped_damage = std::max(any_equipped_damage, item.damage);
    if (item.flags & dis::kItemLeftHand)
      left_hand_damage = std::max(left_hand_damage, item.damage);
    if (item.flags & dis::kItemRightHand)
      state.damage_rating = std::max(state.damage_rating, item.damage);
  }
  // Skyrim presents the active right-hand weapon as the primary damage card.
  // Left hand and then any equipped weapon are deterministic fallbacks for a
  // shield/spell right hand or unusual modded equipment layout.
  if (state.damage_rating <= 0.0f) state.damage_rating = left_hand_damage;
  if (state.damage_rating <= 0.0f) state.damage_rating = any_equipped_damage;
  if (state.damage_rating <= 0.0f) {
    // With no weapon selected, Skyrim's UnarmedDamage actor value is the
    // player-facing damage card rather than an artificial zero.
    state.damage_rating = std::max(0.0f, ActorValue(player, 35, 1));
  }
  if (g_first_live_sample)
    PluginLog(false, "sample: first-live phase=inventory complete items=%u",
              state.item_count);
  // The terrain pass and Scaleform marker layer are intentionally separate.
  // Refresh the private marker cache only when map-page entry requested it;
  // ordinary 100 ms telemetry samples and camera gestures never walk the
  // reference handle array.
  FillMapMarkersCached(state, player);
  if (g_first_live_sample)
    PluginLog(false, "sample: first-live phase=magic begin");
  FillMagicAndFavoritesCached(state, player);
  if (g_first_live_sample)
    PluginLog(false, "sample: first-live phase=magic complete magic=%u",
              state.magic_count);
  if (g_first_live_sample)
    PluginLog(false, "sample: first-live phase=quests begin");
  FillQuests(state);
  if (g_first_live_sample)
    PluginLog(false, "sample: first-live phase=quests complete quests=%u",
              state.quest_count);
  if (g_first_live_sample) {
    uint32_t categories[7] = {};
    for (uint32_t i = 0; i < state.item_count; ++i) {
      const int category = InventoryCategory(
          static_cast<uint8_t>(state.items[i].form_type));
      if (category >= 1 && category <= 6) ++categories[category];
    }
    PluginLog(false,
              "sample: first live snapshot items=%u/%u truncated=%u "
              "[weapons=%u apparel=%u potions=%u ingredients=%u books=%u misc=%u] "
              "magic=%u quests=%u markers=%u",
              state.item_count, g_inventory_discovered_count,
              g_inventory_truncated ? 1u : 0u,
              categories[1], categories[2], categories[3], categories[4],
              categories[5], categories[6], state.magic_count,
              state.quest_count, state.map_marker_count);
    g_first_live_sample = false;
  }
  if (g_submit) {
    const uint64_t submit_started_us = PerfNowUs();
    g_submit(&state);
    RecordPerf(g_perf_submit, submit_started_us);
    g_live_snapshot_ready.store(true, std::memory_order_release);
  }
  const bool native_rendering_enabled =
      g_native_item_models_enabled || g_custom_character_preview_enabled ||
      g_native_character_preview_enabled || g_native_map_enabled;
  if (native_rendering_enabled &&
      g_renderer_ready.load(std::memory_order_acquire) &&
      (!g_capture_present_hooked.load(std::memory_order_acquire) ||
       !g_hud_post_display_hooked.load(std::memory_order_acquire) ||
       !g_inventory_post_display_hooked.load(std::memory_order_acquire))) {
    const bool present_ready = InstallItemPreviewPresentHook();
    const bool hud_ready = InstallHudPostDisplayHook();
    const bool inventory_ready = InstallInventoryPostDisplayHook();
    if ((!present_ready || !hud_ready || !inventory_ready) &&
        now - g_character_render.last_failure_log_ms >= 5000) {
      PluginLog(false,
                "native-render: hooks not ready; preview deferred "
                "present=%u hud_post_display=%u inventory_control=%u",
                present_ready ? 1u : 0u, hud_ready ? 1u : 0u,
                inventory_ready ? 1u : 0u);
      g_character_render.last_failure_log_ms = now;
    }
  }
  if (now - g_last_summary_log >= 10000) {
    void* world_root = ReadOr<void*>(
        reinterpret_cast<void*>(g_base + kWorldRootNodePtr), nullptr);
    void* world_camera = world_root && RegionAllows(world_root, 0x130)
                             ? ReadOr<void*>(
                                   reinterpret_cast<uint8_t*>(world_root) +
                                       0x128,
                                   nullptr)
                             : nullptr;
    PluginLog(false,
              "sample: live seq=%llu epoch=%llu cell=%p world=%08X pos=(%.0f,%.0f,%.0f) "
              "items=%u magic=%u quests=%u markers=%u "
              "native_map={root=%p camera=%p requested=%u}",
              static_cast<unsigned long long>(state.sequence),
              static_cast<unsigned long long>(state.world_epoch), cell,
              state.world_form_id, state.player_x, state.player_y, state.player_z,
              state.item_count, state.magic_count, state.quest_count,
              state.map_marker_count,
              world_root, world_camera,
              GetTickCount64() < g_native_map_requested_until_ms.load(
                                     std::memory_order_acquire)
                  ? 1u
                  : 0u);
    g_last_summary_log = now;
  }
  RecordPerf(g_perf_sample, sample_started_us);
  MaybeLogSamplerPerf(now);
}

DWORD WINAPI Worker(void*) {
  unsigned renderer_retry_ticks = 0;
  while (g_running.load(std::memory_order_acquire)) {
    const bool data_loaded = g_data_loaded.load(std::memory_order_acquire);
    const bool game_ready = g_game_ready.load(std::memory_order_acquire);
    const uint64_t now = GetTickCount64();
    // This worker never reads Skyrim objects. PlayerCharacter::Update samples
    // them on Skyrim's TLS-owning thread; the worker only attaches the panel
    // renderer after that callback has produced a validated live snapshot.
    if (data_loaded && game_ready &&
        g_live_snapshot_ready.load(std::memory_order_acquire) &&
        now >= g_renderer_not_before.load(std::memory_order_acquire) &&
        g_enable_renderer && renderer_retry_ticks == 0) {
      const bool was_ready = g_renderer_ready.load(std::memory_order_acquire);
      const bool enabled = g_enable_renderer() != 0;
      g_renderer_ready.store(enabled, std::memory_order_release);
      if (enabled != was_ready)
        PluginLog(false, "renderer: live-world health changed ready=%u", enabled ? 1u : 0u);
      // Once healthy, this is only a cheap panel-thread health probe. A failed
      // attach still backs off for five seconds to avoid graphics churn.
      renderer_retry_ticks = enabled ? 10u : 50u;
    }
    if (renderer_retry_ticks) --renderer_retry_ticks;

    Sleep(100);
  }
  return 0;
}

}  // namespace

extern "C" __declspec(dllexport) bool SKSEPlugin_Query(const SKSEInterface* skse, PluginInfo* info) {
  if (!skse || !info) return false;
  info->infoVersion = PluginInfo::kInfoVersion;
  info->name = "Skyrim Dual Screen";
  info->version = 1;
  return !skse->isEditor && skse->runtimeVersion == kRuntime1597;
}

extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const SKSEInterface* skse) {
  if (!skse || skse->runtimeVersion != kRuntime1597) return false;
  PluginLog(true,
            "plugin load: Skyrim 1.5.97 / bridge 0x%08X build=%s "
            "item_scheme=%d",
            dis::kBridgeVersion, kPluginBuildIdentity, kItemPreviewScheme);
  LoadNativeRenderingOptions();
  g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  if (g_native_character_preview_enabled &&
      !g_character_clone_exception_handler) {
    g_character_clone_exception_handler = AddVectoredExceptionHandler(
        1, CharacterCloneReleaseExceptionHandler);
    PluginLog(false,
              "character-preview: native release exception probe installed=%u",
              g_character_clone_exception_handler ? 1u : 0u);
  }
  const int show_inventory_3d = g_base && RegionAllows(
      reinterpret_cast<void*>(g_base + kShowInventory3DSetting),
      sizeof(uint8_t))
      ? ReadOr<uint8_t>(
            reinterpret_cast<void*>(g_base + kShowInventory3DSetting), 0)
      : -1;
  PluginLog(false,
            "native-render identity: build=%s effective(items=%u "
            "character_custom=%u character_legacy=%u map=%u) "
            "bShowInventory3D=%d",
            kPluginBuildIdentity,
            g_native_item_models_enabled ? 1u : 0u,
            g_custom_character_preview_enabled ? 1u : 0u,
            g_native_character_preview_enabled ? 1u : 0u,
            g_native_map_enabled ? 1u : 0u,
            show_inventory_3d);
  g_messages = reinterpret_cast<SKSEMessagingInterface*>(skse->QueryInterface(kMessagingInterface));
  g_serialization = reinterpret_cast<SKSESerializationInterface*>(
      skse->QueryInterface(kSerializationInterface));
  HMODULE proxy = GetModuleHandleW(L"dxgi.dll");
  g_submit = ResolveProxyExport<dis::SubmitSnapshotFn>(
      proxy, "SDS_SubmitSnapshot");
  g_submit_item_preview = ResolveProxyExport<dis::SubmitItemPreviewFn>(
      proxy, "SDS_SubmitItemPreview");
  g_poll = ResolveProxyExport<dis::PollCommandFn>(
      proxy, "SDS_PollCommandSized");
  g_has_pending_command = ResolveProxyExport<dis::HasPendingCommandFn>(
      proxy, "SDS_HasPendingCommand");
  g_report_action_success = ResolveProxyExport<dis::ReportActionSuccessFn>(
      proxy, "SDS_ReportActionSuccess");
  g_enable_renderer = ResolveProxyExport<dis::EnableRendererFn>(
      proxy, "SDS_EnableRenderer");
  g_get_character_surface =
      ResolveProxyExport<dis::GetCharacterPreviewSurfaceFn>(
          proxy, "SDS_GetCharacterPreviewSurface");
  g_set_character_ready = ResolveProxyExport<dis::SetCharacterPreviewReadyFn>(
      proxy, "SDS_SetCharacterPreviewReady");
  g_submit_character_preview =
      ResolveProxyExport<dis::SubmitCharacterPreviewFn>(
          proxy, "SDS_SubmitCharacterPreview");
  g_submit_map_preview = ResolveProxyExport<dis::SubmitMapPreviewFn>(
      proxy, "SDS_SubmitMapPreview");
  const PluginHandle handle = skse->GetPluginHandle ? skse->GetPluginHandle() : 0xFFFFFFFFu;
  if (!g_base || !g_messages || g_messages->interfaceVersion < 1 ||
      !g_submit || !g_submit_item_preview || !g_poll ||
      !g_has_pending_command || !g_report_action_success ||
      !g_enable_renderer ||
      !g_get_character_surface || !g_set_character_ready ||
      !g_submit_character_preview || !g_submit_map_preview ||
      handle == 0xFFFFFFFFu) {
    PluginLog(false, "plugin load failed: missing messaging/bridge interface");
    return false;
  }

  // Start every fallible resource before registering the irreversible SKSE
  // callback. If this fails, SKSE may safely unload us because it owns no
  // pointer into this DLL and no game code has been modified.
  g_running.store(true, std::memory_order_release);
  g_worker = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
  if (!g_worker) {
    g_running.store(false, std::memory_order_release);
    PluginLog(false, "plugin load failed: worker thread could not be created");
    return false;
  }
  if (!g_messages->RegisterListener(handle, "SKSE", OnSkseMessage)) {
    g_running.store(false, std::memory_order_release);
    // The worker has no unbounded operation before lifecycle flags are armed;
    // wait for it to leave rather than unloading code it could still execute.
    WaitForSingleObject(g_worker, INFINITE);
    CloseHandle(g_worker);
    g_worker = nullptr;
    PluginLog(false, "plugin load failed: SKSE listener registration failed");
    return false;
  }
  if (g_custom_character_preview_enabled) {
    dis::player_preview::Initialize(g_base, g_submit_character_preview,
                                    &PreviewLogLine);
  }
  // Register hotbar persistence only after the last fallible initialization.
  // These SKSE callbacks retain pointers into this module just like the
  // message listener, so no failure path may unload the DLL after this point.
  if (g_serialization && g_serialization->SetUniqueID &&
      g_serialization->SetSaveCallback &&
      g_serialization->SetLoadCallback &&
      g_serialization->SetRevertCallback) {
    g_serialization->SetUniqueID(handle, kSerializationUniqueId);
    g_serialization->SetSaveCallback(handle, &OnSerializationSave);
    g_serialization->SetLoadCallback(handle, &OnSerializationLoad);
    g_serialization->SetRevertCallback(handle, &OnSerializationRevert);
    PluginLog(false, "hotbar: SKSE serialization callbacks registered");
  } else {
    PluginLog(false,
              "hotbar: SKSE serialization unavailable; assignments remain "
              "live for this session only");
  }
  // RegisterListener and the worker both retain pointers into this module, so
  // never ask SKSE to unload us after this point. A hook mismatch is logged and
  // leaves the game untouched; the renderer remains safely dormant.
  const bool hooked = InstallPlayerUpdateHook();
  const bool map_hooked =
      g_native_map_enabled && InstallMainRenderWorldHook();
  if (g_native_map_enabled && !map_hooked) g_native_map_enabled = false;
  PluginLog(false,
            "plugin load complete; PlayerCharacter update sampling armed=%u "
            "native_map_world_hook=%u",
            hooked ? 1u : 0u, map_hooked ? 1u : 0u);
  return true;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
  return TRUE;
}
