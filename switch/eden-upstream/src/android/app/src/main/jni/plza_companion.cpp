#include "jni/plza_companion.h"
#include "core/loader/plza_item_protocol.h"
#include "jni/plza_inventory_reader.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <vector>

#ifdef PLZA_EXTERNAL_MODULE
#include "host_shim.h"
#else
#include "common/common_types.h"
#include "common/logging.h"
#include "core/core.h"
#include "core/hle/kernel/k_process.h"
#include "core/memory.h"
#include "jni/native.h"
#endif

namespace PlzaCompanion {
namespace {

constexpr u64 TitleId = 0x0100F43008C44000ULL;
constexpr u32 Magic = 0x415A4C50;
constexpr std::array<u8, 0x20> BuildId{
    0xB1, 0xF1, 0x2F, 0xD9, 0x19, 0xEA, 0xE8, 0x6A,
    0xB8, 0xA9, 0x78, 0x31, 0x76, 0x77, 0xE6, 0x4B,
    0xCE, 0x44, 0x3D, 0x1F, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

constexpr VAddr RuntimeRegistryGotOffset = 0x03EC26E8;
constexpr VAddr RuntimeRegistryReadyOffset = 0xA600;
constexpr VAddr SceneControllerOffset = 0xA078;
constexpr VAddr UiSceneManagerReadyOffset = 0x0610EE00;
constexpr VAddr FieldRuntimePointerOffset = 0x06136D70;
constexpr VAddr FieldCameraControlOffset = 0x160;
constexpr VAddr FieldCameraObjectOffset = 0x168;
constexpr VAddr FieldCameraRotationOffset = 0xD20;
constexpr std::size_t ItemActionMailboxSize = 0x1000;
constexpr std::size_t ItemActionRequestOffset = 0x00;
constexpr std::size_t ItemActionCompletedOffset = 0x08;
constexpr std::size_t ItemActionStateOffset = 0x10;
constexpr std::size_t ItemActionOperationOffset = 0x14;
constexpr std::size_t ItemActionItemOffset = 0x18;
constexpr std::size_t ItemActionPartyTargetOffset = 0x1C;
constexpr std::size_t ItemActionPouchOffset = 0x20;
constexpr std::size_t ItemActionAuxOffset = 0x24;
constexpr std::size_t ItemActionResultOffset = 0x28;
constexpr u32 ItemActionMailboxIdle = 0;
constexpr u32 ItemActionMailboxQueued = 1;
constexpr u32 ItemActionMailboxRunning = 2;
constexpr u32 ItemActionMailboxSucceeded = 3;
constexpr u32 ItemActionMailboxRejected = 4;

constexpr std::array<VAddr, 4> ItemBlockPointer{0x0610A670, 0x30, 0x08, 0x480};
constexpr VAddr ItemTableSlotOffset = 0x480;
constexpr VAddr SaveBlockTableScanSize = 0x2000;
constexpr VAddr SaveBlockTableSlotSize = 0x10;

constexpr u32 PartyBlockKey = 0x3AA1A9AD;
constexpr u32 ItemBlockKey = 0x21C9BD44;
constexpr u32 MyStatusBlockKey = 0xE3E89BD1;
constexpr u32 PokedexBlockKey = 0x2D87BE5C;
constexpr u32 MoneyBlockKey = 0x4F35D0DD;
constexpr u32 CoordinatesBlockKey = 0x910D381F;
constexpr u32 QuestBlockKey = 0xB9B223B9;
constexpr u32 MableStatusBlockKey = 0x85DBDCE9;
constexpr u32 InfiniteRoyaleBlockKey = 0x8929BFB6;
constexpr u32 TicketPointsBlockKey = 0x9A730DE1;
constexpr u32 InfiniteTicketPointsBlockKey = 0x1D7EE369;
constexpr u32 SurveyPointsBlockKey = 0x0235471C;

constexpr u32 PartyBlockSize = 0xB40;
constexpr u32 ItemBlockSize = 0xBC00;
constexpr u32 MyStatusBlockSize = 0x78;
constexpr u32 PokedexBlockSize = 0x27068;
constexpr u32 CoordinatesBlockSize = 0x70;
constexpr u32 QuestBlockSize = 0x4000;
constexpr u32 MableStatusBlockSize = 0x0C;
constexpr u32 InfiniteRoyaleBlockSize = 0x18;

constexpr std::size_t PartySlotSize = 0x1E0;
constexpr std::size_t PokemonPartySize = 0x158;
constexpr std::size_t PokemonStoredSize = 0x148;
constexpr std::size_t PokemonBlockSize = 0x50;
constexpr std::size_t ItemRecordSize = 0x10;
constexpr std::size_t ItemRecordCount = 3000;
constexpr std::size_t InlineItemCapacity = 20;
constexpr std::size_t SnapshotItemCapacity = 512;
constexpr std::size_t InlineQuestCapacity = 16;
constexpr std::size_t SnapshotQuestCapacity = 24;
constexpr std::size_t PokedexEntrySize = 0x84;
constexpr std::size_t CoordinateGraphCapacity = 128;
constexpr std::size_t CoordinateCandidateCapacity = 192;
constexpr std::size_t CoordinateNodeBytes = 0x200;
constexpr u64 EmptyEventWorkKey = 0xCBF29CE484222645ULL;
constexpr u64 PrologueQuestWorkKey = 0x496A81FE151FD180ULL;

enum class Status : u32 {
    NotRunning = 0,
    WrongTitle = 1,
    UnsupportedBuild = 2,
    WaitingForGame = 3,
    WaitingForSave = 4,
    Active = 5,
};

enum class ItemActionOperation : int {
    SelectFieldBall = 1,
    UseOnParty = 2,
    GiveToParty = 3,
    UseKeyItem = 4,
    TeachMove = 5,
    EquipMegaStone = 6,
};

enum Availability : u32 {
    Player = 1U << 0,
    Party = 1U << 1,
    Bag = 1U << 2,
    Quests = 1U << 3,
    Map = 1U << 4,
    Pokedex = 1U << 5,
    Rewards = 1U << 6,
};

#pragma pack(push, 1)
struct PartyEntry {
    u16 species;
    u16 form;
    u16 level;
    u16 current_hp;
    u16 maximum_hp;
    u16 status;
    u8 gender;
    u8 flags;
    u16 held_item;
    u32 experience;
    char name[28];
};

struct ItemEntry {
    u32 item_id;
    u16 quantity;
    u8 pocket;
    u8 flags;
    char name[24];
};

struct QuestEntry {
    u32 quest_id;
    u8 type;
    u8 state;
    u8 progress;
    u8 total;
    char title[36];
    char description[36];
};

struct MapMarker {
    u32 marker_id;
    u8 type;
    u8 state;
    u16 icon;
    float x;
    float z;
};

struct PokedexEntry {
    u16 species;
    u8 form;
    u8 flags;
};

struct SnapshotFrame {
    u32 magic;
    u16 version;
    u16 size;
    u32 sequence;
    u32 status;
    u32 availability;
    u32 game_state;
    u32 zone_id;
    u32 money;
    u32 party_count;
    u32 item_count;
    u32 quest_count;
    u32 marker_count;
    float player_x;
    float player_y;
    float player_z;
    u32 pokedex_entry_count;
    PartyEntry party[6];
    ItemEntry items[20];
    QuestEntry quests[InlineQuestCapacity];
    MapMarker markers[16];
    PokedexEntry pokedex[256];
    u32 pokedex_seen;
    u32 pokedex_caught;
    u32 pokedex_mega;
    u32 mable_level;
    u32 mable_level_viewed;
    u32 research_points;
    u32 ticket_points;
    u32 infinite_ticket_points;
    u32 royale_wins;
    u32 prize_medals;
    u32 survey_points;
    float player_heading;
    u8 reserved[496];
    ItemEntry extra_items[SnapshotItemCapacity - InlineItemCapacity];
    QuestEntry extra_quests[SnapshotQuestCapacity - InlineQuestCapacity];
};
#pragma pack(pop)

static_assert(sizeof(PartyEntry) == 48);
static_assert(sizeof(ItemEntry) == 32);
static_assert(sizeof(QuestEntry) == 80);
static_assert(sizeof(MapMarker) == 16);
static_assert(sizeof(PokedexEntry) == 4);
static_assert(sizeof(SnapshotFrame) == 20480);

std::atomic<u32> Sequence{0};
std::atomic<u64> LoggedProcess{0};
std::atomic<u32> LoggedStatus{static_cast<u32>(Status::NotRunning)};
std::atomic<u64> LoggedInitializationSignature{0};
std::atomic<u64> LoggedDataSignature{0};
std::atomic<u64> LoggedBlockSignature{0};

struct LiveBlockCache {
    u64 process_id{};
    VAddr table{};
    VAddr party{};
    VAddr items{};
    VAddr my_status{};
    VAddr pokedex{};
    VAddr money{};
    VAddr coordinates{};
    VAddr quests{};
    VAddr mable_status{};
    VAddr infinite_royale{};
    VAddr ticket_points{};
    VAddr infinite_ticket_points{};
    VAddr survey_points{};
    std::chrono::steady_clock::time_point next_refresh{};
};

LiveBlockCache Blocks{};

struct LiveSnapshotCache {
    u64 process_id{};
    SnapshotFrame frame{};
    std::vector<u8> scratch_block{};
    std::chrono::steady_clock::time_point next_player{};
    std::chrono::steady_clock::time_point next_party{};
    std::chrono::steady_clock::time_point next_bag{};
    std::chrono::steady_clock::time_point next_quests{};
    std::chrono::steady_clock::time_point next_pokedex{};
    std::chrono::steady_clock::time_point next_rewards{};
    std::chrono::steady_clock::time_point next_map_log{};
    bool map_log_initialized{};
    float logged_map_x{};
    float logged_map_z{};
    float logged_map_heading{};
    VAddr logged_camera_object{};
    bool camera_log_initialized{};
    bool item_log_initialized{};
    bool party_log_initialized{};
    std::array<std::array<u32, 3>, ItemRecordCount> logged_items{};
    VAddr logged_live_bag_owner{};
    bool live_bag_log_initialized{};
    bool live_bag_log_available{};
    u64 logged_native_dispatch{};
    std::array<u64, 4> logged_native_handlers{};
    std::array<u64, 2> logged_ball_selection{};
};

LiveSnapshotCache Telemetry{};

struct CoordinateCandidate {
    VAddr address{};
    u8 layout{};
    float x{};
    float y{};
    float z{};
};

struct CoordinateGraphNode {
    VAddr address{};
    u8 depth{};
};

struct LiveCoordinateCache {
    u64 process_id{};
    VAddr scene_controller{};
    VAddr address{};
    u8 layout{};
    bool has_previous{};
    float previous_x{};
    float previous_y{};
    float previous_z{};
    std::array<CoordinateCandidate, CoordinateCandidateCapacity> candidates{};
    std::size_t candidate_count{};
    std::chrono::steady_clock::time_point next_scan{};
    std::chrono::steady_clock::time_point next_diagnostic{};
};

LiveCoordinateCache LiveCoordinates{};
std::mutex BlocksMutex;
std::atomic<u64> LoggedItemActionCompletion{0};
std::atomic<u64> LoggedItemActionProcess{0};
struct ItemBridgeCache {
    u64 process{};
    u32 tick{};
    std::chrono::steady_clock::time_point last_tick{};
    std::chrono::steady_clock::time_point queued_at{};
    std::chrono::steady_clock::time_point next_party{};
    std::chrono::steady_clock::time_point party_received{};
    PlzaItem::PartyChannel party{};
    std::array<u8, PlzaItem::ItemKindsCount> item_kinds{};
};
ItemBridgeCache ItemBridge{}; // protected by BlocksMutex
u64 ItemRequestSequence{}; // protected by BlocksMutex

template <typename T>
std::optional<T> Read(Core::Memory::Memory& memory, VAddr address) {
    if (!address || !memory.IsValidVirtualAddressRange(address, sizeof(T))) {
        return std::nullopt;
    }
    T value{};
    if (!memory.ReadBlockUnsafe(address, &value, sizeof(value))) {
        return std::nullopt;
    }
    return value;
}

bool IsItemActionMailboxAvailable(Core::System& system, VAddr address) {
    if (!address || (address & 7U) != 0) {
        return false;
    }
    auto& memory = system.ApplicationMemory();
    return memory.IsValidVirtualAddressRange(address, ItemActionMailboxSize) &&
           memory.GetPointerSilent(address) != nullptr &&
           memory.GetPointerSilent(address + ItemActionMailboxSize - 1) != nullptr;
}

template <typename T>
T LoadItemActionMailbox(Core::System& system, VAddr mailbox, std::size_t offset, int ordering) {
    const auto* pointer =
        system.ApplicationMemory().GetPointer<T>(mailbox + static_cast<VAddr>(offset));
    return pointer ? __atomic_load_n(pointer, ordering) : T{};
}

template <typename T>
void StoreItemActionMailbox(Core::System& system, VAddr mailbox, std::size_t offset, T value,
                            int ordering) {
    auto* pointer =
        system.ApplicationMemory().GetPointer<T>(mailbox + static_cast<VAddr>(offset));
    if (pointer) {
        __atomic_store_n(pointer, value, ordering);
    }
}

void LogItemActionCompletion(Core::System& system) {
    const VAddr mailbox = system.GetApplicationProcessCompanionMailbox();
    if (!IsItemActionMailboxAvailable(system, mailbox)) {
        return;
    }
    const auto* process = system.ApplicationProcess();
    const u64 process_id = process ? process->GetProcessId() : 0;
    if (LoggedItemActionProcess.exchange(process_id, std::memory_order_relaxed) != process_id) {
        LoggedItemActionCompletion.store(0, std::memory_order_relaxed);
    }
    const u64 completed = LoadItemActionMailbox<u64>(
        system, mailbox, ItemActionCompletedOffset, __ATOMIC_ACQUIRE);
    if (!completed || LoggedItemActionCompletion.exchange(completed, std::memory_order_relaxed) ==
                          completed) {
        return;
    }
    const u32 state = LoadItemActionMailbox<u32>(
        system, mailbox, ItemActionStateOffset, __ATOMIC_ACQUIRE);
    const u32 operation = LoadItemActionMailbox<u32>(
        system, mailbox, ItemActionOperationOffset, __ATOMIC_RELAXED);
    const u32 item = LoadItemActionMailbox<u32>(
        system, mailbox, ItemActionItemOffset, __ATOMIC_RELAXED);
    const s32 party_target = LoadItemActionMailbox<s32>(
        system, mailbox, ItemActionPartyTargetOffset, __ATOMIC_RELAXED);
    const u32 tab = LoadItemActionMailbox<u32>(
        system, mailbox, ItemActionPouchOffset, __ATOMIC_RELAXED);
    const u32 index = LoadItemActionMailbox<u32>(
        system, mailbox, ItemActionAuxOffset, __ATOMIC_RELAXED);
    const u32 result = LoadItemActionMailbox<u32>(
        system, mailbox, ItemActionResultOffset, __ATOMIC_RELAXED);
    if (state == ItemActionMailboxSucceeded) {
        LOG_INFO(Frontend,
                 "[PLZA-ITEM-ACTION] native-confirmed request={} operation={} item={} "
                 "party_target={} pouch={} index={} state={} result={} "
                 "verification={}",
                 completed, operation, item, party_target, tab, index, state, result,
                 operation == 1 ? "persistent-selection" : "native-effect-and-quantity");
    } else {
        const char* reason = result == PlzaItem::StaleSession ? "scene-or-inventory-changed"
                             : result == PlzaItem::Restricted ? "field-input-restricted"
                             : result == PlzaItem::ItemMissing ? "item-no-longer-selectable"
                             : result == PlzaItem::SelectionChanged ? "selection-not-confirmed"
                             : result == PlzaItem::Expired ? "game-thread-not-updating"
                             : result == PlzaItem::NoEffect ? "native-eligibility-no-effect"
                             : result == PlzaItem::TargetChanged ? "native-party-target-changed"
                             : result == PlzaItem::EffectUnconfirmed ? "consumed-effect-unconfirmed-do-not-retry"
                                           : "native-context-unavailable";
        LOG_WARNING(Frontend,
                    "[PLZA-ITEM-ACTION] native-rejected request={} operation={} item={} "
                    "party_target={} presenter_tab={} presenter_index={} state={} result={} "
                    "reason={}",
                    completed, operation, item, party_target, tab, index, state, result, reason);
    }
    if (operation == 2) {
        PlzaItem::MedicineChannel medicine{};
        if (system.ApplicationMemory().ReadBlockUnsafe(mailbox + PlzaItem::MedicineOffset,
                                                       &medicine, sizeof(medicine)))
            LOG_INFO(Frontend, "[PLZA-MEDICINE] request={} target={} stage={} consumed={} "
                     "quantity={}->{} hp={}->{} status={}->{} condition={}->{}",
                     completed, party_target, medicine.stage, medicine.consumed,
                     medicine.quantity_before, medicine.quantity_after, medicine.hp_before,
                     medicine.hp_after, medicine.status_before, medicine.status_after,
                     medicine.condition_before, medicine.condition_after);
        ItemBridge.next_party = {};
    }
}


void PopulateItemActionStatus(Core::System& system, SnapshotFrame& frame, u64 process) {
    using M = PlzaItem::Mailbox;
    if (ItemBridge.process != process) { ItemBridge = {}; ItemBridge.process = process; }
    const VAddr mailbox = system.GetApplicationProcessCompanionMailbox();
    if (!IsItemActionMailboxAvailable(system, mailbox) ||
        LoadItemActionMailbox<u32>(system, mailbox, offsetof(M, magic), __ATOMIC_ACQUIRE) != PlzaItem::Magic ||
        LoadItemActionMailbox<u32>(system, mailbox, offsetof(M, version), __ATOMIC_ACQUIRE) != PlzaItem::Version) return;
    const auto now = std::chrono::steady_clock::now();
    const u32 tick = LoadItemActionMailbox<u32>(system, mailbox, offsetof(M, tick), __ATOMIC_ACQUIRE);
    if (tick != ItemBridge.tick) { ItemBridge.tick = tick; ItemBridge.last_tick = now; }
    // Cancel only a request not yet acquired by the guest. Never retry an action
    // with an uncertain result, or allow it to fire later when gameplay resumes.
    auto state = LoadItemActionMailbox<u32>(system, mailbox, offsetof(M, state), __ATOMIC_ACQUIRE);
    if (state == PlzaItem::Queued && now - ItemBridge.queued_at > std::chrono::seconds(3)) {
        auto* p = system.ApplicationMemory().GetPointer<u32>(mailbox + offsetof(M, state));
        u32 expected = PlzaItem::Queued;
        if (__atomic_compare_exchange_n(p, &expected, PlzaItem::Rejected, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            StoreItemActionMailbox<u32>(system, mailbox, offsetof(M, result), PlzaItem::Expired, __ATOMIC_RELAXED);
            const u64 request = LoadItemActionMailbox<u64>(system, mailbox, offsetof(M, request), __ATOMIC_RELAXED);
            StoreItemActionMailbox<u64>(system, mailbox, offsetof(M, completed), request, __ATOMIC_RELEASE);
        }
    }
    const u64 completed = LoadItemActionMailbox<u64>(system, mailbox, offsetof(M, completed), __ATOMIC_ACQUIRE);
    state = LoadItemActionMailbox<u32>(system, mailbox, offsetof(M, state), __ATOMIC_ACQUIRE);
    const u64 request = LoadItemActionMailbox<u64>(system, mailbox, offsetof(M, request), __ATOMIC_ACQUIRE);
    const std::array<u32, 10> status{
        state, LoadItemActionMailbox<u32>(system, mailbox, offsetof(M, operation), __ATOMIC_RELAXED),
        LoadItemActionMailbox<u32>(system, mailbox, offsetof(M, item), __ATOMIC_RELAXED),
        LoadItemActionMailbox<u32>(system, mailbox, offsetof(M, result), __ATOMIC_RELAXED),
        static_cast<u32>(completed), static_cast<u32>(completed >> 32),
        now - ItemBridge.last_tick < std::chrono::seconds(1) ?
            LoadItemActionMailbox<u32>(system, mailbox, offsetof(M, selected_ball), __ATOMIC_ACQUIRE) : 0,
        static_cast<u32>(request), static_cast<u32>(request >> 32), PlzaItem::Magic};
    static_assert(offsetof(SnapshotFrame, reserved) == 3600);
    std::memcpy(frame.reserved, status.data(), sizeof(status));
    const VAddr party_mailbox = mailbox + PlzaItem::PartyOffset;
    const u32 party_completed = LoadItemActionMailbox<u32>(system, party_mailbox, 4, __ATOMIC_ACQUIRE);
    const u32 party_request = LoadItemActionMailbox<u32>(system, party_mailbox, 0, __ATOMIC_ACQUIRE);
    if (party_completed == party_request) {
        if (party_completed != ItemBridge.party.completed) {
            // Single outstanding read request: guest won't write again until we
            // publish the next one, after this copy has finished.
            const auto previous_party = ItemBridge.party;
            if (!system.ApplicationMemory().ReadBlockUnsafe(party_mailbox, &ItemBridge.party, sizeof(ItemBridge.party)))
                ItemBridge.party = {};
            if (!system.ApplicationMemory().ReadBlockUnsafe(mailbox + PlzaItem::ItemKindsOffset,
                    ItemBridge.item_kinds.data(), ItemBridge.item_kinds.size())) ItemBridge.item_kinds = {};
            ItemBridge.party_received = now;
            if (ItemBridge.party.valid) {
                for (u32 i = 0; i < ItemBridge.party.count && i < 6; ++i) {
                    const auto& member = ItemBridge.party.members[i];
                    if (previous_party.members[i].hp != member.hp || previous_party.members[i].status != member.status ||
                        previous_party.members[i].identity != member.identity)
                        LOG_INFO(Frontend, "[PLZA-PARTY-LIVE] slot={} species={} hp={}/{} status={} snapshotHp={}",
                                 i, member.species, member.hp, member.max_hp, member.status, frame.party[i].current_hp);
                }
            }
        }
        if (now >= ItemBridge.next_party) {
            StoreItemActionMailbox<u32>(system, party_mailbox, 0, party_request + 1, __ATOMIC_RELEASE);
            ItemBridge.next_party = now + std::chrono::milliseconds(750);
        }
    }
    const auto& live_party = ItemBridge.party;
    std::fill_n(frame.reserved + 40, SnapshotItemCapacity / 2, u8{});
    const auto [base, size] = system.GetApplicationProcessMainModule();
    const bool current_party = live_party.valid && live_party.count <= 6 && base && size &&
        now - ItemBridge.party_received < std::chrono::milliseconds(1500) &&
        Read<u64>(system.ApplicationMemory(), base + 0x610B810).value_or(0) == live_party.root;
    if (current_party) {
        // 512 four-bit native action kinds fit in the snapshot's existing reserve.
        // Keep the bag's real flags intact; ordering is the same immutable frame.
        for (u32 i = 0; i < frame.item_count && i < SnapshotItemCapacity; ++i) {
            const auto& item = i < InlineItemCapacity ? frame.items[i] : frame.extra_items[i - InlineItemCapacity];
            const u8 kind = item.item_id < ItemBridge.item_kinds.size() ? ItemBridge.item_kinds[item.item_id] : 0;
            frame.reserved[40 + i / 2] |= (kind & 15) << ((i & 1) * 4);
        }
        frame.party_count = live_party.count;
        for (u32 i = 0; i < live_party.count; ++i) {
            const auto& member = live_party.members[i];
            auto& entry = frame.party[i];
            if (entry.species != member.species || entry.form != member.form) entry = {};
            entry.species = member.species; entry.form = member.form; entry.level = member.level;
            entry.current_hp = member.hp; entry.maximum_hp = member.max_hp; entry.status = member.status;
        }
        frame.availability |= Availability::Party;
    }
}

bool ReadBytes(Core::Memory::Memory& memory, VAddr address, void* output, std::size_t size) {
    if (!address || !output || !size || !memory.IsValidVirtualAddressRange(address, size)) {
        return false;
    }
    return memory.ReadBlockUnsafe(address, output, size);
}

template <typename T>
T Load(const u8* data) {
    T value{};
    std::memcpy(&value, data, sizeof(value));
    return value;
}

bool DecodeCoordinates(const u8* data, u8 layout, float& x, float& y, float& z) {
    const float first = Load<float>(data);
    const float second = Load<float>(data + 4);
    const float third = Load<float>(data + 8);
    if (layout == 1) {
        x = first;
        y = second;
        z = third;
    } else if (layout == 2) {
        x = first;
        y = third;
        z = second;
    } else {
        return false;
    }
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
           std::abs(x) < 100'000.0F && std::abs(y) < 100'000.0F &&
           std::abs(z) < 100'000.0F;
}

class SCXorShift32 {
public:
    explicit SCXorShift32(u32 seed) : state{seed} {
        for (int index = 0; index < std::popcount(seed); ++index) {
            state = Advance(state);
        }
    }

    u8 Next() {
        const u8 value = static_cast<u8>(state >> (counter * 8));
        if (++counter == 4) {
            counter = 0;
            state = Advance(state);
        }
        return value;
    }

private:
    static u32 Advance(u32 value) {
        value ^= value << 2;
        value ^= value >> 15;
        value ^= value << 13;
        return value;
    }

    u32 state{};
    u32 counter{};
};

template <std::size_t Count>
VAddr ResolvePointerAll(Core::Memory::Memory& memory, VAddr main_base,
                        const std::array<VAddr, Count>& jumps) {
    static_assert(Count > 1);
    VAddr address = main_base + jumps.front();
    for (std::size_t index = 1; index < jumps.size(); ++index) {
        const auto pointer = Read<u64>(memory, address);
        if (!pointer || !*pointer) {
            return 0;
        }
        address = *pointer + jumps[index];
    }
    return address;
}

bool DecodeObjectHeader(const std::array<u8, 5>& encrypted, u32 key, u32 expected_size) {
    SCXorShift32 crypto{key};
    if ((encrypted[0] ^ crypto.Next()) != 4) {
        return false;
    }
    std::array<u8, 4> size_bytes{};
    for (std::size_t index = 0; index < size_bytes.size(); ++index) {
        size_bytes[index] = encrypted[index + 1] ^ crypto.Next();
    }
    return Load<u32>(size_bytes.data()) == expected_size;
}

bool ValidateObjectBlock(Core::Memory::Memory& memory, VAddr address, u32 key, u32 size) {
    std::array<u8, 5> encrypted{};
    return ReadBytes(memory, address, encrypted.data(), encrypted.size()) &&
           DecodeObjectHeader(encrypted, key, size);
}

bool DecodeU32Block(const std::array<u8, 5>& encrypted, u32 key, u32& value) {
    SCXorShift32 crypto{key};
    if ((encrypted[0] ^ crypto.Next()) != 10) {
        return false;
    }
    std::array<u8, 4> value_bytes{};
    for (std::size_t index = 0; index < value_bytes.size(); ++index) {
        value_bytes[index] = encrypted[index + 1] ^ crypto.Next();
    }
    value = Load<u32>(value_bytes.data());
    return value <= 99'999'999;
}

bool ReadObjectBlock(Core::Memory::Memory& memory, VAddr address, u32 key, u32 expected_size,
                     std::vector<u8>& output) {
    if (!ValidateObjectBlock(memory, address, key, expected_size)) {
        return false;
    }
    output.resize(expected_size);
    if (!ReadBytes(memory, address + 5, output.data(), output.size())) {
        return false;
    }
    SCXorShift32 crypto{key};
    for (std::size_t index = 0; index < 5; ++index) {
        crypto.Next();
    }
    for (auto& value : output) {
        value ^= crypto.Next();
    }
    return true;
}

void ResetBlocks(u64 process_id) {
    Blocks = {};
    Blocks.process_id = process_id;
    if (Telemetry.process_id != process_id) {
        Telemetry = {};
        Telemetry.process_id = process_id;
    }
}

bool HasAllBlockAddresses() {
    return Blocks.party && Blocks.items && Blocks.my_status && Blocks.pokedex && Blocks.money &&
           Blocks.coordinates && Blocks.quests && Blocks.mable_status && Blocks.infinite_royale &&
           Blocks.ticket_points && Blocks.infinite_ticket_points && Blocks.survey_points;
}

void ScanBlockTable(Core::Memory::Memory& memory) {
    for (VAddr offset = 0; offset < SaveBlockTableScanSize && !HasAllBlockAddresses();
         offset += SaveBlockTableSlotSize) {
        const VAddr block = Read<u64>(memory, Blocks.table + offset + 8).value_or(0);
        if (!block || !memory.IsValidVirtualAddressRange(block, 5)) {
            continue;
        }
        std::array<u8, 5> encrypted{};
        if (!ReadBytes(memory, block, encrypted.data(), encrypted.size())) {
            continue;
        }
        if (!Blocks.party && DecodeObjectHeader(encrypted, PartyBlockKey, PartyBlockSize)) {
            Blocks.party = block;
        }
        if (!Blocks.items && DecodeObjectHeader(encrypted, ItemBlockKey, ItemBlockSize)) {
            Blocks.items = block;
        }
        if (!Blocks.my_status &&
            DecodeObjectHeader(encrypted, MyStatusBlockKey, MyStatusBlockSize)) {
            Blocks.my_status = block;
        }
        if (!Blocks.pokedex && DecodeObjectHeader(encrypted, PokedexBlockKey, PokedexBlockSize)) {
            Blocks.pokedex = block;
        }
        if (!Blocks.coordinates &&
            DecodeObjectHeader(encrypted, CoordinatesBlockKey, CoordinatesBlockSize)) {
            Blocks.coordinates = block;
        }
        if (!Blocks.quests && DecodeObjectHeader(encrypted, QuestBlockKey, QuestBlockSize)) {
            Blocks.quests = block;
        }
        if (!Blocks.mable_status &&
            DecodeObjectHeader(encrypted, MableStatusBlockKey, MableStatusBlockSize)) {
            Blocks.mable_status = block;
        }
        if (!Blocks.infinite_royale &&
            DecodeObjectHeader(encrypted, InfiniteRoyaleBlockKey, InfiniteRoyaleBlockSize)) {
            Blocks.infinite_royale = block;
        }
        if (!Blocks.money) {
            u32 money{};
            if (DecodeU32Block(encrypted, MoneyBlockKey, money)) {
                Blocks.money = block;
            }
        }
        if (!Blocks.ticket_points) {
            u32 value{};
            if (DecodeU32Block(encrypted, TicketPointsBlockKey, value)) {
                Blocks.ticket_points = block;
            }
        }
        if (!Blocks.infinite_ticket_points) {
            u32 value{};
            if (DecodeU32Block(encrypted, InfiniteTicketPointsBlockKey, value)) {
                Blocks.infinite_ticket_points = block;
            }
        }
        if (!Blocks.survey_points) {
            u32 value{};
            if (DecodeU32Block(encrypted, SurveyPointsBlockKey, value)) {
                Blocks.survey_points = block;
            }
        }
    }
}

bool EnsureBlockAddresses(Core::System& system, Core::Memory::Memory& memory, VAddr main_base) {
    const auto* process = system.ApplicationProcess();
    const u64 process_id = process ? process->GetProcessId() : 0;
    if (!process_id) {
        ResetBlocks(0);
        return false;
    }
    if (Blocks.process_id != process_id) {
        ResetBlocks(process_id);
    }

    const auto now = std::chrono::steady_clock::now();
    const bool cached_item_valid =
        Blocks.items && ValidateObjectBlock(memory, Blocks.items, ItemBlockKey, ItemBlockSize);
    if (cached_item_valid && now < Blocks.next_refresh) {
        return true;
    }
    if (!cached_item_valid) {
        const u64 saved_process_id = Blocks.process_id;
        ResetBlocks(saved_process_id);
    }

    const VAddr item_slot = ResolvePointerAll(memory, main_base, ItemBlockPointer);
    const VAddr item_block = item_slot ? Read<u64>(memory, item_slot + 8).value_or(0) : 0;
    if (!item_slot || item_slot < ItemTableSlotOffset ||
        !ValidateObjectBlock(memory, item_block, ItemBlockKey, ItemBlockSize)) {
        Blocks.next_refresh = now + std::chrono::seconds(1);
        return false;
    }

    const VAddr table = item_slot - ItemTableSlotOffset;
    if (Blocks.table != table || Blocks.items != item_block) {
        const u64 saved_process_id = Blocks.process_id;
        ResetBlocks(saved_process_id);
        Blocks.table = table;
        Blocks.items = item_block;
    }
    ScanBlockTable(memory);
    Blocks.next_refresh = now + std::chrono::seconds(2);
    const u64 block_signature = Blocks.table ^ std::rotl(Blocks.items, 7) ^
                                std::rotl(Blocks.party, 13) ^ std::rotl(Blocks.coordinates, 19) ^
                                std::rotl(Blocks.quests, 29) ^ std::rotl(Blocks.pokedex, 37);
    if (LoggedBlockSignature.exchange(block_signature, std::memory_order_relaxed) !=
        block_signature) {
        LOG_INFO(Frontend,
                 "[PLZA-DATA] save table={:#x} item={:#x} party={:#x} status={:#x} "
                 "pokedex={:#x} money={:#x} coordinates={:#x} quests={:#x} "
                 "rewards={:#x}/{:#x}",
                 Blocks.table, Blocks.items, Blocks.party, Blocks.my_status, Blocks.pokedex,
                 Blocks.money, Blocks.coordinates, Blocks.quests, Blocks.mable_status,
                 Blocks.infinite_royale);
    }
    return true;
}

void CryptPokemonArray(u8* data, std::size_t size, u32 seed) {
    for (std::size_t offset = 0; offset + sizeof(u16) <= size; offset += sizeof(u16)) {
        seed = 0x41C64E6D * seed + 0x6073;
        const u16 value = Load<u16>(data + offset) ^ static_cast<u16>(seed >> 16);
        std::memcpy(data + offset, &value, sizeof(value));
    }
}

void DecryptPokemon(std::array<u8, PokemonPartySize>& data) {
    if (Load<u16>(data.data() + 0x70) == 0 && Load<u16>(data.data() + 0x110) == 0) {
        return;
    }

    u32 seed = Load<u32>(data.data());
    const u32 shuffle_value = (seed >> 13) & 31;
    CryptPokemonArray(data.data() + 8, PokemonStoredSize - 8, seed);
    CryptPokemonArray(data.data() + PokemonStoredSize, PokemonPartySize - PokemonStoredSize,
                      seed);

    constexpr std::array<u8, 128> BlockPosition{
        0, 1, 2, 3, 0, 1, 3, 2, 0, 2, 1, 3, 0, 3, 1, 2,
        0, 2, 3, 1, 0, 3, 2, 1, 1, 0, 2, 3, 1, 0, 3, 2,
        2, 0, 1, 3, 3, 0, 1, 2, 2, 0, 3, 1, 3, 0, 2, 1,
        1, 2, 0, 3, 1, 3, 0, 2, 2, 1, 0, 3, 3, 1, 0, 2,
        2, 3, 0, 1, 3, 2, 0, 1, 1, 2, 3, 0, 1, 3, 2, 0,
        2, 1, 3, 0, 3, 1, 2, 0, 2, 3, 1, 0, 3, 2, 1, 0,
        0, 1, 2, 3, 0, 1, 3, 2, 0, 2, 1, 3, 0, 3, 1, 2,
        0, 2, 3, 1, 0, 3, 2, 1, 1, 0, 2, 3, 1, 0, 3, 2,
    };
    if (!shuffle_value) {
        return;
    }

    std::array<u8, 4> permutation{0, 1, 2, 3};
    std::array<u8, 4> slot_of{0, 1, 2, 3};
    const u8* desired_order = BlockPosition.data() + shuffle_value * 4;
    for (u8 index = 0; index < 3; ++index) {
        const u8 desired = desired_order[index];
        const u8 swap_index = slot_of[desired];
        if (swap_index == index) {
            continue;
        }
        const std::size_t left = 8 + index * PokemonBlockSize;
        const std::size_t right = 8 + swap_index * PokemonBlockSize;
        for (std::size_t byte = 0; byte < PokemonBlockSize; ++byte) {
            std::swap(data[left + byte], data[right + byte]);
        }
        const u8 block_at_index = permutation[index];
        permutation[swap_index] = block_at_index;
        slot_of[block_at_index] = swap_index;
    }
}

bool HasValidPokemonChecksum(const std::array<u8, PokemonPartySize>& data) {
    u32 checksum{};
    for (std::size_t offset = 8; offset < PokemonStoredSize; offset += sizeof(u16)) {
        checksum += Load<u16>(data.data() + offset);
    }
    return Load<u16>(data.data() + 4) == 0 &&
           static_cast<u16>(checksum) == Load<u16>(data.data() + 6);
}

void CopyUtf16Name(char* output, std::size_t output_size, const u8* input,
                   std::size_t code_units) {
    if (!output_size) {
        return;
    }
    std::size_t written{};
    for (std::size_t index = 0; index < code_units && written + 1 < output_size; ++index) {
        u32 codepoint = Load<u16>(input + index * sizeof(u16));
        if (!codepoint) {
            break;
        }
        if (codepoint >= 0xD800 && codepoint <= 0xDBFF && index + 1 < code_units) {
            const u32 low = Load<u16>(input + (index + 1) * sizeof(u16));
            if (low >= 0xDC00 && low <= 0xDFFF) {
                codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                ++index;
            } else {
                codepoint = 0xFFFD;
            }
        } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
            codepoint = 0xFFFD;
        }
        std::array<char, 4> encoded{};
        std::size_t encoded_size{};
        if (codepoint <= 0x7F) {
            encoded[0] = static_cast<char>(codepoint);
            encoded_size = 1;
        } else if (codepoint <= 0x7FF) {
            encoded[0] = static_cast<char>(0xC0 | (codepoint >> 6));
            encoded[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
            encoded_size = 2;
        } else if (codepoint <= 0xFFFF) {
            encoded[0] = static_cast<char>(0xE0 | (codepoint >> 12));
            encoded[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            encoded[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
            encoded_size = 3;
        } else if (codepoint <= 0x10FFFF) {
            encoded[0] = static_cast<char>(0xF0 | (codepoint >> 18));
            encoded[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            encoded[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            encoded[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
            encoded_size = 4;
        }
        if (!encoded_size || written + encoded_size >= output_size) {
            break;
        }
        std::memcpy(output + written, encoded.data(), encoded_size);
        written += encoded_size;
    }
    output[written] = 0;
}

u16 NationalSpecies(u16 internal) {
    static constexpr std::array<s8, 109> InternalToNational{
        65, -1, -1, -1, -1, 31, 31, 47, 47, 29, 29, 53, 31, 31, 46, 44, 30, 30,
        -7, -7, -7, 13, 13, -2, -2, 23, 23, 24, -21, -21, 27, 27, 47, 47, 47, 26,
        14, -33, -33, -33, -17, -17, 3, -29, 12, -12, -31, -31, -31, 3, 3, -24,
        -24, -44, -44, -30, -30, -28, -28, 23, 23, 6, 7, 29, 8, 3, 4, 4, 20, 4,
        23, 6, 3, 3, 4, -1, 13, 9, 7, 5, 7, 9, 9, -43, -43, -43, -68, -68, -68,
        -58, -58, -25, -29, -31, 6, -1, 6, 0, 0, 0, 3, 3, 4, 2, 3, 3, -5, -12,
        -12,
    };
    constexpr u16 FirstUnaligned = 917;
    if (internal < FirstUnaligned) {
        return internal;
    }
    const std::size_t index = internal - FirstUnaligned;
    if (index >= InternalToNational.size()) {
        return internal;
    }
    return static_cast<u16>(static_cast<s32>(internal) + InternalToNational[index]);
}

bool PopulateParty(Core::Memory::Memory& memory, SnapshotFrame& frame) {
    if (!Blocks.party) {
        return false;
    }
    auto& block = Telemetry.scratch_block;
    if (!ReadObjectBlock(memory, Blocks.party, PartyBlockKey, PartyBlockSize, block)) {
        Blocks.party = 0;
        return false;
    }

    for (std::size_t slot = 0; slot < 6; ++slot) {
        const u8* source = block.data() + slot * PartySlotSize;
        if (Load<u32>(source) == 0 && Load<u16>(source + 8) == 0) {
            continue;
        }
        std::array<u8, PokemonPartySize> pokemon{};
        std::memcpy(pokemon.data(), source, pokemon.size());
        DecryptPokemon(pokemon);
        if (!HasValidPokemonChecksum(pokemon)) {
            LOG_WARNING(Frontend, "[PLZA-DATA] rejected party slot {} with invalid checksum",
                        slot);
            continue;
        }
        const u16 species = Load<u16>(pokemon.data() + 0x08);
        const u8 level = pokemon[0x148];
        const u16 maximum_hp = Load<u16>(pokemon.data() + 0x14A);
        if (!species || !level || level > 100 || !maximum_hp) {
            continue;
        }
        auto& entry = frame.party[frame.party_count++];
        entry.species = NationalSpecies(species);
        entry.form = pokemon[0x24];
        entry.level = level;
        entry.current_hp = Load<u16>(pokemon.data() + 0x8A);
        entry.maximum_hp = maximum_hp;
        entry.status = static_cast<u16>(Load<u32>(pokemon.data() + 0x90));
        entry.gender = (pokemon[0x22] >> 1) & 3;
        const u32 trainer_id = Load<u32>(pokemon.data() + 0x0C);
        const u32 personality_id = Load<u32>(pokemon.data() + 0x1C);
        const u16 shiny_value = static_cast<u16>(trainer_id) ^
                                static_cast<u16>(trainer_id >> 16) ^
                                static_cast<u16>(personality_id) ^
                                static_cast<u16>(personality_id >> 16);
        entry.flags = (pokemon[0x23] ? 1 : 0) | (shiny_value < 16 ? 2 : 0);
        entry.held_item = Load<u16>(pokemon.data() + 0x0A);
        entry.experience = Load<u32>(pokemon.data() + 0x10);
        CopyUtf16Name(entry.name, sizeof(entry.name), pokemon.data() + 0x58, 13);
    }
    return true;
}

u8 MapPouch(u32 pouch) {
    return pouch <= 7 ? static_cast<u8>(pouch + 1) : 0;
}

u64 HashQuestWork(u32 quest_id) {
    constexpr char Prefix[] = "SYS_WORK_QUEST_";
    u64 hash = EmptyEventWorkKey;
    for (const char value : Prefix) {
        if (!value) {
            break;
        }
        hash = (hash ^ static_cast<u8>(value)) * 0x100000001B3ULL;
    }
    std::array<char, 10> digits{};
    std::size_t count{};
    do {
        digits[count++] = static_cast<char>('0' + quest_id % 10);
        quest_id /= 10;
    } while (quest_id && count < digits.size());
    while (count) {
        hash = (hash ^ static_cast<u8>(digits[--count])) * 0x100000001B3ULL;
    }
    return hash;
}

u64 HashWorkName(const char* name) {
    u64 hash = EmptyEventWorkKey;
    for (const char* cursor = name; *cursor; ++cursor) {
        hash = (hash ^ static_cast<u8>(*cursor)) * 0x100000001B3ULL;
    }
    return hash;
}

std::optional<u32> FindMainQuestId(u64 key) {
    static const auto QuestHashes = [] {
        std::vector<std::pair<u64, u32>> hashes;
        hashes.reserve(1'100);
        std::array<char, 40> name{};
        for (u32 chapter = 0; chapter <= 10; ++chapter) {
            for (u32 quest = 0; quest < 100; ++quest) {
                std::snprintf(name.data(), name.size(), "SYS_WORK_MAINQUEST_%02u_%02u", chapter,
                              quest);
                hashes.emplace_back(HashWorkName(name.data()), chapter * 100 + quest);
            }
        }
        std::ranges::sort(hashes, {}, &std::pair<u64, u32>::first);
        return hashes;
    }();
    const auto found = std::ranges::lower_bound(QuestHashes, key, {},
                                                &std::pair<u64, u32>::first);
    if (found == QuestHashes.end() || found->first != key) {
        return std::nullopt;
    }
    return found->second;
}

std::optional<u32> FindSideQuestId(u64 key) {
    static const auto QuestHashes = [] {
        std::array<std::pair<u64, u32>, 121> hashes{};
        for (u32 quest_id = 1; quest_id <= hashes.size(); ++quest_id) {
            hashes[quest_id - 1] = {HashQuestWork(quest_id), quest_id};
        }
        std::ranges::sort(hashes, {}, &std::pair<u64, u32>::first);
        return hashes;
    }();
    const auto found = std::ranges::lower_bound(QuestHashes, key, {},
                                                &std::pair<u64, u32>::first);
    if (found == QuestHashes.end() || found->first != key) {
        return std::nullopt;
    }
    return found->second;
}

bool ReadCurrentBag(Core::Memory::Memory& memory, VAddr main_base,
                    PlzaInventory::Records& records, VAddr& owner) {
    auto reader = [&](u64 address, void* output, std::size_t size) {
        return ReadBytes(memory, address, output, size);
    };
    return PlzaInventory::ReadLiveBag(reader, main_base, records, owner);
}

bool PopulateItems(Core::Memory::Memory& memory, VAddr main_base, SnapshotFrame& frame) {
    // On-device vanilla Potion: native records 44->43 while the encrypted save
    // mirror stayed at 44. Display and action validation must use the active bag.
    PlzaInventory::Records records{};
    VAddr owner{};
    const bool available = ReadCurrentBag(memory, main_base, records, owner);
    if (!Telemetry.live_bag_log_initialized || available != Telemetry.live_bag_log_available ||
        owner != Telemetry.logged_live_bag_owner) {
        LOG_INFO(Frontend, "[PLZA-LIVE-BAG] available={} owner={:#x} source=typed-component",
                 available, owner);
        Telemetry.item_log_initialized = false;
    }
    Telemetry.live_bag_log_initialized = true;
    Telemetry.live_bag_log_available = available;
    Telemetry.logged_live_bag_owner = owner;
    if (!available) return false; // Do not fall back to a stale save quantity.
    for (u32 item_id = 1; item_id < ItemRecordCount; ++item_id) {
        const auto& record = records[item_id];
        const u32 pouch = record.pouch;
        const s32 quantity = record.quantity;
        const u32 flags = record.flags;
        const std::array<u32, 3> current{pouch, static_cast<u32>(quantity), flags};
        const auto previous = Telemetry.logged_items[item_id];
        if (Telemetry.item_log_initialized && previous != current) {
            LOG_INFO(Frontend,
                     "[PLZA-ITEM-STATE] source=live-bag item={} quantity={}->{} pouch={}->{} flags={:#x}->{:#x}",
                     item_id, static_cast<s32>(previous[1]), quantity, previous[0], pouch,
                     previous[2], flags);
        }
        Telemetry.logged_items[item_id] = current;
        if (pouch == std::numeric_limits<u32>::max() || pouch > 7 || quantity <= 0) {
            continue;
        }
        if (frame.item_count == SnapshotItemCapacity) continue;
        ItemEntry& entry = frame.item_count < InlineItemCapacity
                               ? frame.items[frame.item_count]
                               : frame.extra_items[frame.item_count - InlineItemCapacity];
        ++frame.item_count;
        entry.item_id = item_id;
        entry.quantity = static_cast<u16>(std::min<s32>(quantity, 65535));
        entry.pocket = MapPouch(pouch);
        entry.flags = static_cast<u8>(flags);
    }
    if (!Telemetry.item_log_initialized) {
        LOG_INFO(Frontend, "[PLZA-ITEM-STATE] baseline captured items={}", frame.item_count);
        Telemetry.item_log_initialized = true;
    }
    return true;
}

// Read-only native-call diagnostics. The real presenter is captured at its call
// site; GOT[3EC6D48]+130 is a STRING and must never be sampled as a presenter.
void LogNativeItemState(Core::System& system, Core::Memory::Memory& memory, VAddr main_base) {
    const VAddr mailbox = system.GetApplicationProcessCompanionMailbox();
    if (IsItemActionMailboxAvailable(system, mailbox)) {
        const u64 sequence = LoadItemActionMailbox<u64>(system, mailbox, 0x88, __ATOMIC_ACQUIRE);
        if (sequence != Telemetry.logged_native_dispatch) {
            auto* lock = reinterpret_cast<u32*>(memory.GetPointerSilent(mailbox + 0x80));
            u32 expected = 0;
            if (__atomic_compare_exchange_n(lock, &expected, 1, false, __ATOMIC_ACQUIRE,
                                            __ATOMIC_RELAXED)) {
                std::array<u64, 11> event{};
                for (std::size_t i = 0; i < event.size(); ++i) {
                    event[i] = LoadItemActionMailbox<u64>(system, mailbox, 0x88 + i * 8,
                                                         __ATOMIC_RELAXED);
                }
                StoreItemActionMailbox<u32>(system, mailbox, 0x80, 0, __ATOMIC_RELEASE);
                LOG_INFO(Frontend,
                         "[PLZA-NATIVE-DISPATCH] sequence={} coalesced={} source={} caller={:#x} "
                         "presenter={:#x} request={} item={} list_index={} contexts="
                         "[{:#x},{:#x},{:#x},{:#x}] task_before={:#x} scheduler={:#x}",
                         event[0], event[0] > Telemetry.logged_native_dispatch
                                       ? event[0] - Telemetry.logged_native_dispatch - 1 : 0,
                         event[3] ? "companion-pending" : "vanilla", event[2], event[1], event[3],
                         event[4] & 0xFFFF, (event[4] >> 16) & 0xFFFF,
                         event[5], event[6], event[7], event[8], event[9], event[10]);
                Telemetry.logged_native_dispatch = event[0];
            }
        }
        constexpr std::array<u32, 4> offsets{0x100, 0x180, 0x200, 0x280};
        constexpr std::array<const char*, 4> names{
            "bag-mutation", "medicine-effect", "medicine-target", "select-ball"};
        for (std::size_t index = 0; index < offsets.size(); ++index) {
            const auto offset = offsets[index];
            const u64 sequence = LoadItemActionMailbox<u64>(system, mailbox, offset + 8,
                                                             __ATOMIC_ACQUIRE);
            if (sequence == Telemetry.logged_native_handlers[index]) continue;
            auto* lock = reinterpret_cast<u32*>(memory.GetPointerSilent(mailbox + offset));
            u32 expected = 0;
            if (!__atomic_compare_exchange_n(lock, &expected, 1, false, __ATOMIC_ACQUIRE,
                                             __ATOMIC_RELAXED)) continue;
            std::array<u64, 10> event{};
            for (std::size_t i = 0; i < event.size(); ++i)
                event[i] = LoadItemActionMailbox<u64>(system, mailbox, offset + 8 + i * 8,
                                                     __ATOMIC_RELAXED);
            StoreItemActionMailbox<u32>(system, mailbox, offset, 0, __ATOMIC_RELEASE);
            // SelectBall is a continuously updated field task, not a one-shot
            // equip event. x2 is not an argument to 2BB90C; only context/item
            // changes are useful here. Never reapply a captured selection.
            if (index == 3) {
                const std::array<u64, 2> selection{event[1], event[2]};
                if (selection == Telemetry.logged_ball_selection) {
                    Telemetry.logged_native_handlers[index] = event[0];
                    continue;
                }
                Telemetry.logged_ball_selection = selection;
                event[3] = 0;
            }
            if (index == 0) Telemetry.next_bag = {}; // Native mutation: refresh next UI sample.
            LOG_INFO(Frontend,
                     "[PLZA-NATIVE-HANDLER] handler={} sequence={} coalesced={} "
                     "args=[{:#x},{:#x},{:#x}] caller=main+{:#x} request={} "
                     "objects=[{:#x},{:#x}] vtables=[{:#x},{:#x}] effect_verified=false",
                     names[index], event[0], event[0] > Telemetry.logged_native_handlers[index]
                         ? event[0] - Telemetry.logged_native_handlers[index] - 1 : 0,
                     event[1], event[2], event[3], event[4] >= main_base ? event[4] - main_base : 0,
                     event[5], event[6], event[7], event[8], event[9]);
            Telemetry.logged_native_handlers[index] = event[0];
        }
    }
}

bool PopulateLiveCoordinates(Core::Memory::Memory& memory, VAddr main_base,
                             VAddr runtime_registry, VAddr scene_controller, float saved_x,
                             float saved_y, float saved_z, SnapshotFrame& frame) {
    const u64 process_id = Blocks.process_id;
    if (LiveCoordinates.process_id != process_id) {
        LiveCoordinates = {};
        LiveCoordinates.process_id = process_id;
    }
    if (scene_controller) {
        LiveCoordinates.scene_controller = scene_controller;
    }

    const auto apply_live_coordinates = [&](VAddr address, u8 layout) {
        std::array<u8, 12> values{};
        float x{};
        float y{};
        float z{};
        if (!ReadBytes(memory, address, values.data(), values.size()) ||
            !DecodeCoordinates(values.data(), layout, x, y, z)) {
            return false;
        }
        if (LiveCoordinates.has_previous) {
            const float delta_x = x - LiveCoordinates.previous_x;
            const float delta_y = y - LiveCoordinates.previous_y;
            const float delta_z = z - LiveCoordinates.previous_z;
            const float distance_squared = delta_x * delta_x + delta_y * delta_y;
            if (distance_squared >= 10'000.0F || std::abs(delta_z) >= 50.0F) {
                return false;
            }
        }
        LiveCoordinates.previous_x = x;
        LiveCoordinates.previous_y = y;
        LiveCoordinates.previous_z = z;
        LiveCoordinates.has_previous = true;
        frame.player_x = x;
        frame.player_y = y;
        frame.player_z = z;
        return true;
    };

    if (LiveCoordinates.address) {
        if (apply_live_coordinates(LiveCoordinates.address, LiveCoordinates.layout)) {
            return true;
        }
        LOG_WARNING(Frontend, "[PLZA-MAP] live transform invalidated address={:#x} layout={}",
                    LiveCoordinates.address, LiveCoordinates.layout);
        LiveCoordinates.address = 0;
        LiveCoordinates.layout = 0;
        LiveCoordinates.has_previous = false;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now < LiveCoordinates.next_scan) {
        return false;
    }
    const auto scan_started = now;
    LiveCoordinates.next_scan = now + std::chrono::seconds(1);

    std::array<CoordinateGraphNode, CoordinateGraphCapacity> nodes{};
    std::size_t node_count{};
    const auto add_node = [&](VAddr address, u8 depth) {
        if (!address || node_count >= nodes.size() ||
            !memory.IsValidVirtualAddressRange(address, CoordinateNodeBytes)) {
            return;
        }
        const auto found = std::find_if(nodes.begin(), nodes.begin() + node_count,
                                        [address](const CoordinateGraphNode& node) {
                                            return node.address == address;
                                        });
        if (found == nodes.begin() + node_count) {
            nodes[node_count++] = {address, depth};
        }
    };

    add_node(runtime_registry, 0);
    add_node(runtime_registry ? runtime_registry + 0xA000 : 0, 0);
    add_node(scene_controller, 0);
    add_node(LiveCoordinates.scene_controller, 0);
    add_node(Read<u64>(memory, main_base + ItemBlockPointer.front()).value_or(0), 0);

    std::array<CoordinateCandidate, CoordinateCandidateCapacity> current{};
    std::size_t current_count{};
    std::array<u8, CoordinateNodeBytes> bytes{};
    for (std::size_t node_index = 0; node_index < node_count; ++node_index) {
        const CoordinateGraphNode node = nodes[node_index];
        if (!ReadBytes(memory, node.address, bytes.data(), bytes.size())) {
            continue;
        }
        for (std::size_t offset = 0;
             offset + 12 <= bytes.size() && current_count < current.size(); offset += 4) {
            for (u8 layout = 1; layout <= 2 && current_count < current.size(); ++layout) {
                float x{};
                float y{};
                float z{};
                if (!DecodeCoordinates(bytes.data() + offset, layout, x, y, z) ||
                    std::abs(x - saved_x) > 300.0F || std::abs(y - saved_y) > 300.0F ||
                    std::abs(z - saved_z) > 100.0F) {
                    continue;
                }
                current[current_count++] = {node.address + offset, layout, x, y, z};
            }
        }
        if (node.depth >= 3) {
            continue;
        }
        for (std::size_t offset = 0; offset + sizeof(u64) <= bytes.size(); offset += sizeof(u64)) {
            add_node(Load<u64>(bytes.data() + offset), static_cast<u8>(node.depth + 1));
        }
    }

    const CoordinateCandidate* selected{};
    float selected_score = std::numeric_limits<float>::max();
    std::size_t moving_candidates{};
    for (std::size_t index = 0; index < current_count; ++index) {
        const auto& candidate = current[index];
        const auto previous = std::find_if(
            LiveCoordinates.candidates.begin(),
            LiveCoordinates.candidates.begin() + LiveCoordinates.candidate_count,
            [&candidate](const CoordinateCandidate& value) {
                return value.address == candidate.address && value.layout == candidate.layout;
            });
        if (previous == LiveCoordinates.candidates.begin() + LiveCoordinates.candidate_count) {
            continue;
        }
        const float delta_x = candidate.x - previous->x;
        const float delta_y = candidate.y - previous->y;
        const float delta_z = candidate.z - previous->z;
        const float distance_squared = delta_x * delta_x + delta_y * delta_y;
        if (distance_squared <= 0.0025F || distance_squared >= 2'500.0F ||
            std::abs(delta_z) >= 20.0F) {
            continue;
        }
        ++moving_candidates;
        const float score = std::abs(candidate.x - saved_x) +
                            std::abs(candidate.y - saved_y) +
                            std::abs(candidate.z - saved_z) * 0.25F;
        if (score < selected_score) {
            selected = &candidate;
            selected_score = score;
        }
    }

    LiveCoordinates.candidate_count = current_count;
    std::copy_n(current.begin(), current_count, LiveCoordinates.candidates.begin());
    if (selected) {
        LiveCoordinates.address = selected->address;
        LiveCoordinates.layout = selected->layout;
        LiveCoordinates.has_previous = true;
        LiveCoordinates.previous_x = selected->x;
        LiveCoordinates.previous_y = selected->y;
        LiveCoordinates.previous_z = selected->z;
        LOG_INFO(Frontend,
                 "[PLZA-MAP] live transform resolved address={:#x} layout={} score={:.3f} "
                 "movingCandidates={}",
                 selected->address, selected->layout, selected_score, moving_candidates);
        frame.player_x = selected->x;
        frame.player_y = selected->y;
        frame.player_z = selected->z;
        return true;
    }

    if (now >= LiveCoordinates.next_diagnostic) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                                 std::chrono::steady_clock::now() - scan_started)
                                 .count();
        LOG_INFO(Frontend,
                 "[PLZA-MAP] live transform scan nodes={} candidates={} movingCandidates={} "
                 "elapsedUs={}",
                 node_count, current_count, moving_candidates, elapsed);
        LiveCoordinates.next_diagnostic = now + std::chrono::seconds(10);
    }
    return false;
}

bool PopulateCameraHeading(Core::Memory::Memory& memory, VAddr main_base,
                           SnapshotFrame& frame) {
    const VAddr field_runtime =
        Read<u64>(memory, main_base + FieldRuntimePointerOffset).value_or(0);
    const VAddr camera_control =
        field_runtime
            ? Read<u64>(memory, field_runtime + FieldCameraControlOffset).value_or(0)
            : 0;
    const VAddr camera =
        field_runtime ? Read<u64>(memory, field_runtime + FieldCameraObjectOffset).value_or(0) : 0;
    if (!field_runtime || !camera_control || !camera ||
        !memory.IsValidVirtualAddressRange(camera + FieldCameraRotationOffset,
                                           sizeof(float) * 4)) {
        if (Telemetry.camera_log_initialized) {
            LOG_WARNING(Frontend,
                        "[PLZA-MAP] field camera invalidated runtime={:#x} control={:#x} "
                        "camera={:#x}",
                        field_runtime, camera_control, camera);
            Telemetry.camera_log_initialized = false;
            Telemetry.logged_camera_object = 0;
        }
        return false;
    }

    const float rotation_x =
        Read<float>(memory, camera + FieldCameraRotationOffset).value_or(0.0F);
    const float rotation_y =
        Read<float>(memory, camera + FieldCameraRotationOffset + 4).value_or(0.0F);
    const float rotation_z =
        Read<float>(memory, camera + FieldCameraRotationOffset + 8).value_or(0.0F);
    const float rotation_w =
        Read<float>(memory, camera + FieldCameraRotationOffset + 12).value_or(0.0F);
    const float norm_squared = rotation_x * rotation_x + rotation_y * rotation_y +
                               rotation_z * rotation_z + rotation_w * rotation_w;
    if (!std::isfinite(norm_squared) || norm_squared < 0.25F || norm_squared > 2.25F) {
        return false;
    }

    const float inverse_norm = 1.0F / std::sqrt(norm_squared);
    const float x = rotation_x * inverse_norm;
    const float y = rotation_y * inverse_norm;
    const float z = rotation_z * inverse_norm;
    const float w = rotation_w * inverse_norm;
    const float sin_yaw = 2.0F * (w * z + x * y);
    const float cos_yaw = 1.0F - 2.0F * (y * y + z * z);
    if (!std::isfinite(sin_yaw) || !std::isfinite(cos_yaw) ||
        (std::abs(sin_yaw) < 0.000001F && std::abs(cos_yaw) < 0.000001F)) {
        return false;
    }

    constexpr float RadiansToDegrees = 57.29577951308232F;
    frame.player_heading = std::atan2(sin_yaw, cos_yaw) * RadiansToDegrees;
    if (frame.player_heading < 0.0F) {
        frame.player_heading += 360.0F;
    }
    if (!Telemetry.camera_log_initialized || Telemetry.logged_camera_object != camera) {
        LOG_INFO(Frontend,
                 "[PLZA-MAP] field camera resolved runtime={:#x} control={:#x} camera={:#x} "
                 "rotation=({:.4f},{:.4f},{:.4f},{:.4f}) heading={:.2f}",
                 field_runtime, camera_control, camera, x, y, z, w, frame.player_heading);
        Telemetry.camera_log_initialized = true;
        Telemetry.logged_camera_object = camera;
    }
    return true;
}

bool PopulateCoordinates(Core::Memory::Memory& memory, VAddr main_base, VAddr runtime_registry,
                         VAddr scene_controller, SnapshotFrame& frame) {
    if (!Blocks.coordinates) {
        return false;
    }
    auto& block = Telemetry.scratch_block;
    if (!ReadObjectBlock(memory, Blocks.coordinates, CoordinatesBlockKey, CoordinatesBlockSize,
                         block)) {
        Blocks.coordinates = 0;
        return false;
    }
    const float x = Load<float>(block.data() + 0x20);
    const float z = Load<float>(block.data() + 0x24);
    const float y = Load<float>(block.data() + 0x28);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || block[0] == 0) {
        return false;
    }
    frame.player_x = x;
    frame.player_y = y;
    frame.player_z = z;
    const float rotation_z = Load<float>(block.data() + 0x34);
    const float rotation_w = Load<float>(block.data() + 0x3C);
    if (std::isfinite(rotation_z) && std::isfinite(rotation_w) &&
        (std::abs(rotation_z) > 0.0001F || std::abs(rotation_w) > 0.0001F)) {
        constexpr float RadiansToDegrees = 57.29577951308232F;
        frame.player_heading = std::atan2(rotation_z, rotation_w) * 2.0F * RadiansToDegrees;
        if (frame.player_heading < 0.0F) {
            frame.player_heading += 360.0F;
        }
    }
    PopulateLiveCoordinates(memory, main_base, runtime_registry, scene_controller, x, y, z,
                            frame);
    PopulateCameraHeading(memory, main_base, frame);
    u32 zone{};
    for (std::size_t index = 0; index < 0x20 && block[index]; ++index) {
        if (block[index] >= '0' && block[index] <= '9') {
            zone = zone * 10 + static_cast<u32>(block[index] - '0');
        }
    }
    frame.zone_id = zone;
    const auto now = std::chrono::steady_clock::now();
    const bool changed = !Telemetry.map_log_initialized ||
                         std::abs(frame.player_x - Telemetry.logged_map_x) > 0.05F ||
                         std::abs(frame.player_y - Telemetry.logged_map_z) > 0.05F ||
                         std::abs(frame.player_heading - Telemetry.logged_map_heading) > 1.0F;
    if (changed && now >= Telemetry.next_map_log) {
        LOG_INFO(Frontend,
                 "[PLZA-MAP] coordinates x={:.3f} y={:.3f} z={:.3f} heading={:.2f} zone={}",
                 frame.player_x, frame.player_y, frame.player_z, frame.player_heading,
                 frame.zone_id);
        Telemetry.map_log_initialized = true;
        Telemetry.logged_map_x = frame.player_x;
        Telemetry.logged_map_z = frame.player_y;
        Telemetry.logged_map_heading = frame.player_heading;
        Telemetry.next_map_log = now + std::chrono::seconds(1);
    }
    return true;
}

bool PopulateMoney(Core::Memory::Memory& memory, SnapshotFrame& frame) {
    if (!Blocks.money) {
        return false;
    }
    std::array<u8, 5> encrypted{};
    u32 value{};
    if (!ReadBytes(memory, Blocks.money, encrypted.data(), encrypted.size()) ||
        !DecodeU32Block(encrypted, MoneyBlockKey, value)) {
        Blocks.money = 0;
        return false;
    }
    frame.money = value;
    return true;
}

bool PopulateMyStatus(Core::Memory::Memory& memory) {
    if (!Blocks.my_status) {
        return false;
    }
    auto& block = Telemetry.scratch_block;
    if (!ReadObjectBlock(memory, Blocks.my_status, MyStatusBlockKey, MyStatusBlockSize, block)) {
        Blocks.my_status = 0;
        return false;
    }
    return std::any_of(block.begin(), block.end(), [](u8 value) { return value != 0; });
}

bool PopulatePokedex(Core::Memory::Memory& memory, SnapshotFrame& frame) {
    if (!Blocks.pokedex) {
        return false;
    }
    auto& block = Telemetry.scratch_block;
    if (!ReadObjectBlock(memory, Blocks.pokedex, PokedexBlockKey, PokedexBlockSize, block)) {
        Blocks.pokedex = 0;
        return false;
    }
    const std::size_t entry_count = block.size() / PokedexEntrySize;
    for (std::size_t index = 1; index < entry_count; ++index) {
        const u8* entry = block.data() + index * PokedexEntrySize;
        const bool caught = Load<u32>(entry) != 0;
        const bool seen = Load<u32>(entry + 4) != 0;
        const bool shiny = Load<u32>(entry + 0x0C) != 0;
        const bool mega = entry[0x10] != 0;
        const bool alpha = entry[0x11] != 0;
        frame.pokedex_caught += caught;
        frame.pokedex_seen += seen;
        frame.pokedex_mega += mega;
        if (!seen && !caught) {
            continue;
        }
        const u16 species = NationalSpecies(static_cast<u16>(index));
        if (!species || species > 1'025 || frame.pokedex_entry_count >= 256) {
            continue;
        }
        auto& output = frame.pokedex[frame.pokedex_entry_count++];
        output.species = species;
        output.form = entry[0x5A];
        output.flags = (seen ? 1 : 0) | (caught ? 2 : 0) | (shiny ? 4 : 0) |
                       (mega ? 8 : 0) | (alpha ? 16 : 0) | (entry[0x0A] ? 32 : 0) |
                       (entry[0x5C] ? 64 : 0);
    }
    return true;
}

bool PopulateQuests(Core::Memory::Memory& memory, SnapshotFrame& frame) {
    if (!Blocks.quests) {
        return false;
    }
    auto& block = Telemetry.scratch_block;
    if (!ReadObjectBlock(memory, Blocks.quests, QuestBlockKey, QuestBlockSize, block)) {
        Blocks.quests = 0;
        return false;
    }
    for (std::size_t offset = 0;
         offset + 16 <= block.size() && frame.quest_count < SnapshotQuestCapacity;
         offset += 16) {
        const u64 key = Load<u64>(block.data() + offset);
        const u64 value = Load<u64>(block.data() + offset + 8);
        if (key == EmptyEventWorkKey || value == 0) {
            continue;
        }
        const auto main_quest_id = FindMainQuestId(key);
        const auto side_quest_id = FindSideQuestId(key);
        if (!main_quest_id && !side_quest_id) {
            continue;
        }
        QuestEntry& entry = frame.quest_count < InlineQuestCapacity
                                ? frame.quests[frame.quest_count]
                                : frame.extra_quests[frame.quest_count - InlineQuestCapacity];
        ++frame.quest_count;
        entry.quest_id = main_quest_id ? *main_quest_id : *side_quest_id;
        entry.type = main_quest_id ? 0 : 1;
        entry.state = static_cast<u8>(std::min<u64>(value, 255));
        entry.progress = static_cast<u8>(std::min<u64>(value, 255));
    }
    return true;
}

bool PopulateRewards(Core::Memory::Memory& memory, SnapshotFrame& frame) {
    bool research_available = false;
    auto& block = Telemetry.scratch_block;
    if (Blocks.mable_status &&
        ReadObjectBlock(memory, Blocks.mable_status, MableStatusBlockKey, MableStatusBlockSize,
                        block)) {
        frame.mable_level = Load<u32>(block.data());
        frame.mable_level_viewed = Load<u32>(block.data() + 4);
        frame.research_points = Load<u32>(block.data() + 8);
        research_available = true;
    }
    if (Blocks.infinite_royale &&
        ReadObjectBlock(memory, Blocks.infinite_royale, InfiniteRoyaleBlockKey,
                        InfiniteRoyaleBlockSize, block)) {
        frame.royale_wins = Load<u32>(block.data() + 8);
        frame.prize_medals = Load<u32>(block.data() + 0x0C);
    }
    const auto read_scalar = [&](VAddr& address, u32 key, u32& output) {
        if (!address) {
            return false;
        }
        std::array<u8, 5> encrypted{};
        if (!ReadBytes(memory, address, encrypted.data(), encrypted.size()) ||
            !DecodeU32Block(encrypted, key, output)) {
            address = 0;
            return false;
        }
        return true;
    };
    read_scalar(Blocks.ticket_points, TicketPointsBlockKey, frame.ticket_points);
    read_scalar(Blocks.infinite_ticket_points, InfiniteTicketPointsBlockKey,
                frame.infinite_ticket_points);
    read_scalar(Blocks.survey_points, SurveyPointsBlockKey, frame.survey_points);
    return research_available;
}

void PopulateSnapshotData(Core::System& system, Core::Memory::Memory& memory, VAddr main_base,
                          VAddr runtime_registry, VAddr scene_controller, SnapshotFrame& frame) {
    const auto* process = system.ApplicationProcess();
    const u64 process_id = process ? process->GetProcessId() : 0;
    if (Telemetry.process_id != process_id) {
        Telemetry = {};
        Telemetry.process_id = process_id;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now >= Telemetry.next_player) {
        SnapshotFrame candidate = Telemetry.frame;
        candidate.availability &= ~(Availability::Player | Availability::Map);
        if (PopulateMyStatus(memory)) {
            candidate.availability |= Availability::Player;
        }
        if (PopulateCoordinates(memory, main_base, runtime_registry, scene_controller, candidate)) {
            candidate.availability |= Availability::Player | Availability::Map;
        }
        PopulateMoney(memory, candidate);
        Telemetry.frame = candidate;
        Telemetry.next_player = now + std::chrono::milliseconds(200);
    }
    if (now >= Telemetry.next_party) {
        SnapshotFrame candidate = Telemetry.frame;
        candidate.party_count = 0;
        std::memset(candidate.party, 0, sizeof(candidate.party));
        if (PopulateParty(memory, candidate)) {
            for (u32 i = 0; i < candidate.party_count; ++i) {
                const auto& before = Telemetry.frame.party[i];
                const auto& after = candidate.party[i];
                if (!Telemetry.party_log_initialized || before.species != after.species ||
                    before.current_hp != after.current_hp || before.maximum_hp != after.maximum_hp ||
                    before.status != after.status || before.held_item != after.held_item) {
                    LOG_INFO(Frontend,
                             "[PLZA-PARTY-ITEM-STATE] baseline={} slot={} species={}->{} "
                             "held_item={}->{} hp={}->{} max_hp={}->{} status={}->{}",
                             !Telemetry.party_log_initialized, i, before.species, after.species,
                             before.held_item, after.held_item, before.current_hp, after.current_hp,
                             before.maximum_hp, after.maximum_hp, before.status, after.status);
                }
            }
            Telemetry.party_log_initialized = true;
            candidate.availability |= Availability::Party;
            Telemetry.frame = candidate;
        }
        Telemetry.next_party = now + std::chrono::milliseconds(750);
    }
    if (now >= Telemetry.next_bag) {
        SnapshotFrame candidate = Telemetry.frame;
        candidate.item_count = 0;
        std::memset(candidate.items, 0, sizeof(candidate.items));
        std::memset(candidate.extra_items, 0, sizeof(candidate.extra_items));
        candidate.availability &= ~Availability::Bag;
        if (PopulateItems(memory, main_base, candidate)) {
            candidate.availability |= Availability::Bag;
        }
        Telemetry.frame = candidate;
        Telemetry.next_bag = now + std::chrono::milliseconds(750);
    }
    if (now >= Telemetry.next_quests) {
        SnapshotFrame candidate = Telemetry.frame;
        candidate.quest_count = 0;
        std::memset(candidate.quests, 0, sizeof(candidate.quests));
        std::memset(candidate.extra_quests, 0, sizeof(candidate.extra_quests));
        if (PopulateQuests(memory, candidate)) {
            candidate.availability |= Availability::Quests;
            Telemetry.frame = candidate;
        }
        Telemetry.next_quests = now + std::chrono::milliseconds(1500);
    }
    if (now >= Telemetry.next_pokedex) {
        SnapshotFrame candidate = Telemetry.frame;
        candidate.pokedex_entry_count = 0;
        candidate.pokedex_seen = 0;
        candidate.pokedex_caught = 0;
        candidate.pokedex_mega = 0;
        std::memset(candidate.pokedex, 0, sizeof(candidate.pokedex));
        if (PopulatePokedex(memory, candidate)) {
            candidate.availability |= Availability::Pokedex;
            Telemetry.frame = candidate;
        }
        Telemetry.next_pokedex = now + std::chrono::seconds(3);
    }
    if (now >= Telemetry.next_rewards) {
        SnapshotFrame candidate = Telemetry.frame;
        if (PopulateRewards(memory, candidate)) {
            candidate.availability |= Availability::Rewards;
            Telemetry.frame = candidate;
        }
        Telemetry.next_rewards = now + std::chrono::milliseconds(1500);
    }

    const u32 magic = frame.magic;
    LogNativeItemState(system, memory, main_base);
    const u16 version = frame.version;
    const u16 size = frame.size;
    const u32 sequence = frame.sequence;
    const u32 status = frame.status;
    frame = Telemetry.frame;
    frame.magic = magic;
    frame.version = version;
    frame.size = size;
    frame.sequence = sequence;
    frame.status = status;
}

bool IsExpectedBuild(const Core::System& system) {
    return system.GetApplicationProcessBuildID() == BuildId;
}

void LogRuntimeStatus(Core::System& system, VAddr main_base, VAddr main_size, Status status,
                      u32 ui_ready, VAddr runtime_registry, u32 registry_ready,
                      VAddr scene_controller) {
    const auto* process = system.ApplicationProcess();
    const u64 process_id = process ? process->GetProcessId() : 0;
    const u32 status_value = static_cast<u32>(status);
    const u64 previous_process = LoggedProcess.exchange(process_id, std::memory_order_relaxed);
    const u32 previous_status = LoggedStatus.exchange(status_value, std::memory_order_relaxed);
    if (!process_id || (previous_process == process_id && previous_status == status_value)) {
        return;
    }
    LOG_INFO(Frontend,
             "[PLZA Companion] Runtime status changed: pid={} status={} main={:#x} size={:#x} "
             "ui_ready={} registry={:#x} registry_ready={} scene_controller={:#x}",
             process_id, status_value, main_base, main_size, ui_ready, runtime_registry,
             registry_ready, scene_controller);
}

void LogInitializationRequirements(Core::System& system, u32 ui_ready, VAddr runtime_registry,
                                   u32 registry_ready, VAddr scene_controller,
                                   bool live_data_root, u32 availability) {
    const auto* process = system.ApplicationProcess();
    const u64 process_id = process ? process->GetProcessId() : 0;
    const bool world_detected = live_data_root && availability != 0;
    const u64 signature = (process_id << 16) ^ (static_cast<u64>(ui_ready != 0) << 5) ^
                          (static_cast<u64>(runtime_registry != 0) << 4) ^
                          (static_cast<u64>(registry_ready != 0) << 3) ^
                          (static_cast<u64>(scene_controller != 0) << 2) ^
                          (static_cast<u64>(live_data_root) << 1) ^ availability;
    if (!process_id ||
        LoggedInitializationSignature.exchange(signature, std::memory_order_relaxed) == signature) {
        return;
    }
    const char* blocker = !ui_ready                ? "ui-scene-manager"
                          : !runtime_registry      ? "runtime-registry"
                          : !registry_ready        ? "runtime-registry-ready"
                          : !live_data_root        ? "save-block-manager"
                          : !availability          ? "validated-live-block-read"
                                                   : "ready";
    LOG_INFO(Frontend,
             "[PLZA-INIT] worldDetected={} uiSceneManager={} runtimeRegistry={} "
             "registryReady={} sceneController={:#x} liveDataRoot={} availability={:#x} "
             "firstValidGameState={} blocker={}",
             world_detected, ui_ready != 0, runtime_registry != 0, registry_ready != 0,
             scene_controller, live_data_root, availability, world_detected, blocker);
}

void LogDataAvailability(Core::System& system, const SnapshotFrame& frame) {
    const auto* process = system.ApplicationProcess();
    const u64 process_id = process ? process->GetProcessId() : 0;
    const u64 signature = (process_id << 32) ^ (static_cast<u64>(frame.availability) << 24) ^
                          (static_cast<u64>(frame.party_count) << 16) ^
                          (static_cast<u64>(frame.item_count) << 8) ^ frame.quest_count;
    if (!process_id || LoggedDataSignature.exchange(signature, std::memory_order_relaxed) ==
                           signature) {
        return;
    }
    LOG_INFO(Frontend,
             "[PLZA-DATA] availability={:#x} party={} items={} quests={} dexSeen={} "
             "dexCaught={} dexMega={} map=({}, {}, {}) money={}",
             frame.availability, frame.party_count, frame.item_count, frame.quest_count,
             frame.pokedex_seen, frame.pokedex_caught, frame.pokedex_mega, frame.player_x,
             frame.player_y, frame.player_z, frame.money);
}

}

int FillSnapshot(EmulationSession& session, void* output, std::size_t output_size) {
    if (!output || output_size < sizeof(SnapshotFrame)) {
        return -static_cast<int>(sizeof(SnapshotFrame));
    }

    SnapshotFrame frame{};
    frame.magic = Magic;
    frame.version = 4;
    frame.size = sizeof(frame);
    frame.sequence = Sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    frame.status = static_cast<u32>(Status::NotRunning);

    if (!session.IsRunning()) {
        std::memcpy(output, &frame, sizeof(frame));
        return sizeof(frame);
    }

    auto& system = session.System();
    if (system.GetApplicationProcessProgramID() != TitleId) {
        frame.status = static_cast<u32>(Status::WrongTitle);
        std::memcpy(output, &frame, sizeof(frame));
        return sizeof(frame);
    }

    const auto [main_base, main_size] = system.GetApplicationProcessMainModule();
    const auto build_id = system.GetApplicationProcessBuildID();
    if (!main_base || !main_size ||
        std::all_of(build_id.begin(), build_id.end(), [](u8 value) { return value == 0; })) {
        frame.status = static_cast<u32>(Status::WaitingForGame);
        std::memcpy(output, &frame, sizeof(frame));
        return sizeof(frame);
    }
    if (!IsExpectedBuild(system)) {
        frame.status = static_cast<u32>(Status::UnsupportedBuild);
        std::memcpy(output, &frame, sizeof(frame));
        return sizeof(frame);
    }

    auto& memory = system.ApplicationMemory();
    const u32 ui_ready = Read<u32>(memory, main_base + UiSceneManagerReadyOffset).value_or(0);
    const VAddr runtime_registry =
        Read<u64>(memory, main_base + RuntimeRegistryGotOffset).value_or(0);
    const u32 registry_ready = runtime_registry
                                   ? Read<u32>(memory, runtime_registry + RuntimeRegistryReadyOffset)
                                         .value_or(0)
                                   : 0;
    const VAddr scene_controller = runtime_registry
                                       ? Read<u64>(memory, runtime_registry + SceneControllerOffset)
                                             .value_or(0)
                                       : 0;
    const std::scoped_lock blocks_lock{BlocksMutex};
    LogItemActionCompletion(system);
    const auto* application_process = system.ApplicationProcess();
    const u64 process_id = application_process ? application_process->GetProcessId() : 0;
    if (LiveCoordinates.process_id != process_id) {
        LiveCoordinates = {};
        LiveCoordinates.process_id = process_id;
    }
    if (scene_controller) {
        LiveCoordinates.scene_controller = scene_controller;
    }
    const bool live_data_root = EnsureBlockAddresses(system, memory, main_base);
    if (live_data_root) {
        PopulateSnapshotData(system, memory, main_base, runtime_registry, scene_controller, frame);
    }
    const bool world_detected = live_data_root && frame.availability != 0;
    const Status runtime_status = world_detected ? Status::Active
                                  : runtime_registry && registry_ready
                                      ? Status::WaitingForSave
                                      : Status::WaitingForGame;
    frame.game_state = (ui_ready != 0 ? 1U : 0U) | (runtime_registry != 0 ? 2U : 0U) |
                       (registry_ready != 0 ? 4U : 0U) | (scene_controller != 0 ? 8U : 0U) |
                       (live_data_root ? 16U : 0U);
    frame.status = static_cast<u32>(runtime_status);
    PopulateItemActionStatus(system, frame, process_id);
    LogRuntimeStatus(system, main_base, main_size, runtime_status, ui_ready, runtime_registry,
                     registry_ready, scene_controller);
    LogInitializationRequirements(system, ui_ready, runtime_registry, registry_ready,
                                  scene_controller, live_data_root, frame.availability);
    LogDataAvailability(system, frame);

    std::memcpy(output, &frame, sizeof(frame));
    return sizeof(frame);
}

ItemActionResult PerformItemAction(EmulationSession& session, int operation, int item_id,
                                   int party_target) {
    if (!session.IsRunning()) {
        LOG_WARNING(Frontend,
                    "[PLZA-ITEM-ACTION] rejected operation={} item={} target={} "
                    "reason=session-not-running",
                    operation, item_id, party_target);
        return ItemActionResult::SessionNotRunning;
    }
    auto& system = session.System();
    if (system.GetApplicationProcessProgramID() != TitleId) {
        LOG_WARNING(Frontend,
                    "[PLZA-ITEM-ACTION] rejected operation={} item={} target={} "
                    "reason=wrong-title",
                    operation, item_id, party_target);
        return ItemActionResult::WrongTitle;
    }
    if (!IsExpectedBuild(system)) {
        LOG_WARNING(Frontend,
                    "[PLZA-ITEM-ACTION] rejected operation={} item={} target={} "
                    "reason=unsupported-build",
                    operation, item_id, party_target);
        return ItemActionResult::UnsupportedBuild;
    }
    const auto requested_operation = static_cast<ItemActionOperation>(operation);
    if (operation < static_cast<int>(ItemActionOperation::SelectFieldBall) ||
        operation > static_cast<int>(ItemActionOperation::EquipMegaStone) || item_id <= 0 ||
        item_id >= static_cast<int>(ItemRecordCount) || party_target < -1 || party_target >= 6) {
        LOG_WARNING(Frontend,
                    "[PLZA-ITEM-ACTION] rejected operation={} item={} target={} "
                    "reason=invalid-request",
                    operation, item_id, party_target);
        return ItemActionResult::InvalidRequest;
    }

    const auto [main_base, main_size] = system.GetApplicationProcessMainModule();
    auto& memory = system.ApplicationMemory();
    const std::scoped_lock blocks_lock{BlocksMutex};
    if (!main_base || !main_size) {
        LOG_WARNING(Frontend,
                    "[PLZA-ITEM-ACTION] rejected operation={} item={} target={} "
                    "reason=bag-not-ready",
                    operation, item_id, party_target);
        return ItemActionResult::BagNotReady;
    }
    PlzaInventory::Records records{};
    VAddr bag_owner{};
    if (!ReadCurrentBag(memory, main_base, records, bag_owner)) {
        LOG_WARNING(Frontend,
                    "[PLZA-ITEM-ACTION] rejected operation={} item={} target={} "
                    "reason=bag-read-failed",
                    operation, item_id, party_target);
        return ItemActionResult::BagReadFailed;
    }
    const auto& record = records[static_cast<std::size_t>(item_id)];
    const u32 pouch = record.pouch;
    const s32 quantity = record.quantity;
    const u32 flags = record.flags;
    if (pouch == std::numeric_limits<u32>::max() || pouch > 7 || quantity <= 0) {
        LOG_WARNING(Frontend,
                    "[PLZA-ITEM-ACTION] rejected operation={} item={} target={} quantity={} "
                    "pouch={} flags={:#x} reason=item-not-in-bag",
                    operation, item_id, party_target, quantity, pouch, flags);
        return ItemActionResult::ItemNotInBag;
    }

    const bool target_required = requested_operation == ItemActionOperation::UseOnParty ||
                                 requested_operation == ItemActionOperation::GiveToParty ||
                                 requested_operation == ItemActionOperation::TeachMove ||
                                 requested_operation == ItemActionOperation::EquipMegaStone;
    if (target_required && party_target < 0) {
        LOG_WARNING(Frontend,
                    "[PLZA-ITEM-ACTION] rejected operation={} item={} quantity={} pouch={} "
                    "flags={:#x} reason=party-target-required",
                    operation, item_id, quantity, pouch, flags);
        return ItemActionResult::TargetRequired;
    }
    if (target_required &&
        (!ItemBridge.party.valid || static_cast<u32>(party_target) >= ItemBridge.party.count ||
         !ItemBridge.party.members[static_cast<std::size_t>(party_target)].valid ||
         std::chrono::steady_clock::now() - ItemBridge.party_received > std::chrono::milliseconds(1500))) {
        LOG_WARNING(Frontend,
                    "[PLZA-ITEM-ACTION] rejected operation={} item={} target={} party_count={} "
                    "reason=invalid-party-target",
                    operation, item_id, party_target, Telemetry.frame.party_count);
        return ItemActionResult::InvalidTarget;
    }

    const bool item_class_matches =
        (requested_operation == ItemActionOperation::SelectFieldBall && pouch == 1) ||
        (requested_operation == ItemActionOperation::UseOnParty && (pouch == 0 || pouch == 2 || pouch == 5)) ||
        (requested_operation == ItemActionOperation::GiveToParty && (pouch == 2 || pouch == 5)) ||
        (requested_operation == ItemActionOperation::UseKeyItem && pouch == 4) ||
        (requested_operation == ItemActionOperation::TeachMove && pouch == 6) ||
        (requested_operation == ItemActionOperation::EquipMegaStone && pouch == 7);
    if (!item_class_matches) {
        LOG_WARNING(Frontend,
                    "[PLZA-ITEM-ACTION] rejected operation={} item={} target={} quantity={} "
                    "pouch={} flags={:#x} reason=operation-item-class-mismatch",
                    operation, item_id, party_target, quantity, pouch, flags);
        return ItemActionResult::UnsupportedItemClass;
    }

    if (requested_operation == ItemActionOperation::SelectFieldBall ||
        requested_operation == ItemActionOperation::UseOnParty) {
        using M = PlzaItem::Mailbox;
        const VAddr mailbox = system.GetApplicationProcessCompanionMailbox();
        const auto now = std::chrono::steady_clock::now();
        if (!IsItemActionMailboxAvailable(system, mailbox) ||
            LoadItemActionMailbox<u32>(system, mailbox, offsetof(M, magic), __ATOMIC_ACQUIRE) != PlzaItem::Magic ||
            LoadItemActionMailbox<u32>(system, mailbox, offsetof(M, version), __ATOMIC_ACQUIRE) != PlzaItem::Version ||
            now - ItemBridge.last_tick > std::chrono::seconds(1)) return ItemActionResult::GuestThreadBridgeUnavailable;
        const u32 state = LoadItemActionMailbox<u32>(system, mailbox, offsetof(M, state), __ATOMIC_ACQUIRE);
        const u64 request = LoadItemActionMailbox<u64>(system, mailbox, offsetof(M, request), __ATOMIC_ACQUIRE);
        if (state == PlzaItem::Queued || state == PlzaItem::Running ||
            request != LoadItemActionMailbox<u64>(system, mailbox, offsetof(M, completed), __ATOMIC_ACQUIRE))
            return ItemActionResult::ActionBusy;
        const VAddr root = Read<u64>(memory, main_base + 0x610B810).value_or(0);
        const VAddr control = Read<u64>(memory, root + 0x38).value_or(0);
        const u64 token = Read<u64>(memory, control).value_or(0);
        if (!root || !control || !token ||
            Read<u64>(memory, control + 0x10).value_or(0) != root + 0x28 ||
            !Read<u16>(memory, control + 0x1a).value_or(0)) return ItemActionResult::ContextUnavailable;
        if (requested_operation == ItemActionOperation::UseOnParty) {
            if (ItemBridge.party.root != root || ItemBridge.party.root_token != token)
                return ItemActionResult::ContextUnavailable;
            const auto& member = ItemBridge.party.members[static_cast<std::size_t>(party_target)];
            PlzaItem::MedicineChannel medicine{};
            medicine.expected_pokemon = member.identity;
            medicine.expected_token = member.token;
            if (!memory.WriteBlock(mailbox + PlzaItem::MedicineOffset, &medicine, sizeof(medicine)))
                return ItemActionResult::ContextUnavailable;
        }
        StoreItemActionMailbox<u64>(system, mailbox, offsetof(M, expected_root), root, __ATOMIC_RELAXED);
        StoreItemActionMailbox<u64>(system, mailbox, offsetof(M, expected_token), token, __ATOMIC_RELAXED);
        StoreItemActionMailbox<u64>(system, mailbox, offsetof(M, expected_bag), bag_owner, __ATOMIC_RELAXED);
        StoreItemActionMailbox<u32>(system, mailbox, offsetof(M, operation), operation, __ATOMIC_RELAXED);
        StoreItemActionMailbox<u32>(system, mailbox, offsetof(M, item), item_id, __ATOMIC_RELAXED);
        StoreItemActionMailbox<s32>(system, mailbox, offsetof(M, target), party_target, __ATOMIC_RELAXED);
        StoreItemActionMailbox<u32>(system, mailbox, offsetof(M, pouch), pouch, __ATOMIC_RELAXED);
        StoreItemActionMailbox<u32>(system, mailbox, offsetof(M, result), 0, __ATOMIC_RELAXED);
        StoreItemActionMailbox<u64>(system, mailbox, offsetof(M, request), ++ItemRequestSequence, __ATOMIC_RELAXED);
        ItemBridge.queued_at = now;
        StoreItemActionMailbox<u32>(system, mailbox, offsetof(M, state), PlzaItem::Queued, __ATOMIC_RELEASE);
        LOG_INFO(Frontend, "[PLZA-ITEM-ACTION] queued request={} operation={} item={} target={} quantity={}",
                 ItemRequestSequence, operation, item_id, party_target, quantity);
        return ItemActionResult::Queued;
    }
    // Native comparison proved GOT[3EC6D48]+130/+138 are string fields.
    // Do not queue the old writer, even after opening the Satchel. The read-only
    // diagnostic observers capture the actual native handlers without retaining
    // menu pointers. This is explicitly NOT a completed item action.
    LOG_WARNING(Frontend,
                "[PLZA-ITEM-ACTION] rejected operation={} item={} party_target={} quantity={} "
                "pouch={} reason=native-handler-not-yet-validated live_bag_available={}",
                operation, item_id, party_target, quantity, pouch,
                Telemetry.live_bag_log_available);
    return ItemActionResult::NativeHandlerNotReady;
}

bool PerformAction(EmulationSession& session, int action, int argument) {
    return PerformItemAction(session, action, argument, -1) == ItemActionResult::Queued;
}

}
