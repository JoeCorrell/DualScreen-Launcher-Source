


#include "jni/mk8d_companion.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>

#include "common/common_types.h"
#include "common/logging.h"
#include "core/core.h"
#include "core/hle/kernel/k_process.h"
#include "core/memory.h"
#include "jni/native.h"

namespace Mk8dCompanion {
namespace {

constexpr u64 TitleId = 0x0100152000022000ULL;
constexpr std::array<u8, 0x20> BuildId{
    0xFE, 0x94, 0x1E, 0xD5, 0xBA, 0x14, 0xBE, 0x5D,
    0x50, 0x56, 0x98, 0xDA, 0x1B, 0xBF, 0x4F, 0xE7,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

constexpr VAddr FrameworkRootOffset = 0x012F6388;
constexpr VAddr SceneObjectEngineOffset = 0x1B0;
constexpr VAddr SceneActiveFlagsOffset = 0x180;
constexpr u32 SceneActiveMask = 0x04;
constexpr VAddr ObjectRaceDirectorOffset = 0x218;
constexpr VAddr ObjectKartDirectorOffset = 0x238;
constexpr VAddr ObjectItemDirectorOffset = 0x240;
constexpr VAddr KartDirectorHoldersOffset = 0xC8;
constexpr VAddr KartHolderVehicleOffset = 0x08;
constexpr VAddr KartVehicleMoveOffset = 0x28;
constexpr VAddr KartVehiclePlayerIdOffset = 0xA8;
constexpr VAddr KartVehicleMasterOffset = 0xD0;
constexpr VAddr KartMovePositionPointerOffset = 0x50;
constexpr VAddr RaceDirectorLapRankCheckerOffset = 0x58;
constexpr VAddr RaceDirectorPlayerCountOffset = 0x60;
constexpr VAddr RaceDirectorCheckersOffset = 0x68;
constexpr VAddr RaceKartCheckerRankOffset = 0x40;
constexpr VAddr RaceKartCheckerCompletedLapOffset = 0x44;
constexpr VAddr RaceKartCheckerCoinOffset = 0x50;
constexpr VAddr LapRankCheckerLapCountOffset = 0x64;

constexpr VAddr RaceInfoRuleOffset = 0x08;
constexpr VAddr RaceInfoDriverArrayOffset = 0x30;
constexpr VAddr RaceInfoDriverStride = 0x1C;
constexpr VAddr RaceInfoDriverIdOffset = 0x0C;
constexpr VAddr RaceInfoDriverVariantOffset = 0x10;
constexpr VAddr RaceInfoPlayerTypeOffset = 0x14;
constexpr VAddr RaceInfoPlayerCountOffset = 0x180;
constexpr VAddr RaceInfoCourseIdOffset = 0x1A0;

constexpr VAddr ItemOwnerPlayerIdOffset = 0x40;
constexpr VAddr ItemOwnerSlot0Offset = 0x60;
constexpr VAddr ItemOwnerSlot1Offset = 0x68;
constexpr VAddr ItemSlotStateOffset = 0x41;
constexpr VAddr ItemSlotIndexOffset = 0x68;
constexpr VAddr ItemSlotOwnerOffset = 0x70;
constexpr VAddr ItemSlotUseCountOffset = 0x98;
constexpr VAddr ItemSlotStockItemOffset = 0x9C;
constexpr VAddr ItemSlotStockCountOffset = 0xA0;
constexpr VAddr ItemSlotCurrentItemOffset = 0xC8;

constexpr int MaxRacers = 12;


constexpr u32 MaxDriverId = 52;
constexpr u32 MaxRawCourseId = 0x7A;
constexpr int NoItem = -1;
constexpr int MaxItem = 22;
constexpr size_t MailboxRequestOffset = 0;
constexpr size_t MailboxCompletedOffset = 8;
constexpr size_t MailboxStateOffset = 16;
constexpr size_t MailboxSize = 0x20;
constexpr u32 MailboxIdle = 0;
constexpr u32 MailboxQueued = 1;
constexpr u32 MailboxRunning = 2;
constexpr u32 MailboxSucceeded = 3;
constexpr u32 MailboxRejected = 4;
constexpr u8 Slot0RollingFlag = 1U << 0;
constexpr u8 Slot1RollingFlag = 1U << 1;

enum class Status : u32 {
    NotRunning = 0,
    WrongTitle = 1,
    UnsupportedBuild = 2,
    WaitingForRace = 3,
    Active = 4,
};

#pragma pack(push, 1)
struct RacerFrame {
    u8 active;
    u8 local;
    u8 rank;
    u8 lap;
    s16 driver_id;
    s8 item0;
    s8 item1;
    u8 item0_count;
    u8 item1_count;
    u8 driver_variant;
    u8 coins;
    float x;
    float y;
    float z;
};

struct SnapshotFrame {
    u32 magic;
    u16 version;
    u16 size;
    u32 sequence;
    u32 status;
    u32 course_id;
    u8 player_count;
    s8 local_index;
    u8 flags;
    u8 lap_total;
    RacerFrame racers[MaxRacers];
};
#pragma pack(pop)

static_assert(sizeof(RacerFrame) == 24);
static_assert(sizeof(SnapshotFrame) == 312);

std::atomic<u32> Sequence{0};

struct ItemOwnerCache {
    std::mutex mutex;
    u64 process_id{};
    VAddr item_director{};
    VAddr owner_array{};
};

ItemOwnerCache OwnerCache;
std::mutex ActionMutex;

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

bool IsExpectedBuild(const Core::System& system) {
    return system.GetApplicationProcessBuildID() == BuildId;
}

bool IsMailboxAvailable(Core::System& system, VAddr address) {
    if (!address || (address & 7U) != 0) {
        return false;
    }
    auto& memory = system.ApplicationMemory();
    return memory.IsValidVirtualAddressRange(address, MailboxSize) &&
           memory.GetPointerSilent(address) != nullptr &&
           memory.GetPointerSilent(address + MailboxSize - 1) != nullptr;
}

template <typename T>
T LoadMailbox(Core::System& system, VAddr mailbox, size_t offset, int ordering) {
    const auto* pointer =
        system.ApplicationMemory().GetPointer<T>(mailbox + static_cast<VAddr>(offset));
    return pointer ? __atomic_load_n(pointer, ordering) : T{};
}

template <typename T>
void StoreMailbox(Core::System& system, VAddr mailbox, size_t offset, T value, int ordering) {
    auto* pointer = system.ApplicationMemory().GetPointer<T>(mailbox + static_cast<VAddr>(offset));
    if (pointer) {
        __atomic_store_n(pointer, value, ordering);
    }
}

struct FrameworkPointers {
    VAddr scene{};
    VAddr object_engine{};
    VAddr race_info{};
};

std::optional<FrameworkPointers> ResolveFramework(Core::Memory::Memory& memory,
                                                  VAddr main_base) {
    const auto root_cell = Read<u64>(memory, main_base + FrameworkRootOffset);
    const auto root = root_cell && *root_cell ? Read<u64>(memory, *root_cell) : std::nullopt;
    if (!root || !*root) {
        return std::nullopt;
    }

    FrameworkPointers out{};
    const auto scene_holder = Read<u64>(memory, *root + 0x20);
    const auto scene = scene_holder && *scene_holder ? Read<u64>(memory, *scene_holder + 0x08)
                                                     : std::nullopt;
    if (scene && *scene) {
        const auto flags = Read<u32>(memory, *scene + SceneActiveFlagsOffset);


        if (flags && (*flags & SceneActiveMask) != 0) {
            out.scene = *scene;
            out.object_engine = Read<u64>(memory, out.scene + SceneObjectEngineOffset).value_or(0);
        }
    }

    const auto race_holder = Read<u64>(memory, *root + 0x28);
    const auto race_engine = race_holder && *race_holder
                                 ? Read<u64>(memory, *race_holder + 0x240)
                                 : std::nullopt;
    out.race_info = race_engine && *race_engine
                        ? Read<u64>(memory, *race_engine + 0x10).value_or(0)
                        : 0;
    return out;
}

bool ValidateItemOwner(Core::Memory::Memory& memory, VAddr owner, int expected_player,
                       bool require_expected_player) {
    if (!owner) {
        return false;
    }
    const auto player = Read<u32>(memory, owner + ItemOwnerPlayerIdOffset);
    const auto slot0 = Read<u64>(memory, owner + ItemOwnerSlot0Offset);
    const auto slot1 = Read<u64>(memory, owner + ItemOwnerSlot1Offset);
    if (!player || *player >= MaxRacers || !slot0 || !*slot0 || !slot1 || !*slot1 ||
        (require_expected_player && static_cast<int>(*player) != expected_player)) {
        return false;
    }
    const auto index0 = Read<u32>(memory, *slot0 + ItemSlotIndexOffset);
    const auto index1 = Read<u32>(memory, *slot1 + ItemSlotIndexOffset);
    const auto owner0 = Read<u64>(memory, *slot0 + ItemSlotOwnerOffset);
    const auto owner1 = Read<u64>(memory, *slot1 + ItemSlotOwnerOffset);
    return index0 && index1 && *index0 == 0 && *index1 == 1 && owner0 && owner1 &&
           *owner0 == owner && *owner1 == owner;
}

VAddr FindItemOwnerArray(Core::Memory::Memory& memory, VAddr item_director, int player_count,
                         u64 process_id) {
    {
        std::scoped_lock lock{OwnerCache.mutex};
        if (OwnerCache.process_id == process_id && OwnerCache.item_director == item_director &&
            OwnerCache.owner_array) {
            const auto owner = Read<u64>(memory, OwnerCache.owner_array);
            if (owner && ValidateItemOwner(memory, *owner, 0, false)) {
                return OwnerCache.owner_array;
            }
            OwnerCache.owner_array = 0;
        }
    }

    const int validate_count = std::clamp(player_count, 1, MaxRacers);
    for (VAddr offset = 0x38; offset <= 0x160; offset += sizeof(u64)) {
        const auto candidate = Read<u64>(memory, item_director + offset);
        if (!candidate || !*candidate) {
            continue;
        }
        std::array<bool, MaxRacers> seen{};
        bool valid = true;
        for (int i = 0; i < validate_count; ++i) {
            const auto owner = Read<u64>(memory, *candidate + static_cast<VAddr>(i) * 8);
            if (!owner || !ValidateItemOwner(memory, *owner, i, false)) {
                valid = false;
                break;
            }
            const auto player = Read<u32>(memory, *owner + ItemOwnerPlayerIdOffset);
            if (!player || *player >= MaxRacers || seen[*player]) {
                valid = false;
                break;
            }
            seen[*player] = true;
        }
        if (valid) {
            std::scoped_lock lock{OwnerCache.mutex};
            OwnerCache.process_id = process_id;
            OwnerCache.item_director = item_director;
            OwnerCache.owner_array = *candidate;
            LOG_INFO(Frontend, "[MK8D Companion] Resolved ItemOwner array at +0x{:X}", offset);
            return *candidate;
        }
    }
    return 0;
}

VAddr FindOwnerByPlayer(Core::Memory::Memory& memory, VAddr owner_array, int player_count,
                        int player_id) {
    if (!owner_array || player_id < 0 || player_id >= MaxRacers) {
        return 0;
    }
    for (int i = 0; i < std::clamp(player_count, 1, MaxRacers); ++i) {
        const auto owner = Read<u64>(memory, owner_array + static_cast<VAddr>(i) * 8);
        if (!owner || !*owner) {
            continue;
        }
        const auto owner_player = Read<u32>(memory, *owner + ItemOwnerPlayerIdOffset);
        if (owner_player && static_cast<int>(*owner_player) == player_id &&
            ValidateItemOwner(memory, *owner, player_id, true)) {
            return *owner;
        }
    }
    return 0;
}

std::array<VAddr, MaxRacers> MapOwnersByPlayer(Core::Memory::Memory& memory, VAddr owner_array,
                                               int player_count) {
    std::array<VAddr, MaxRacers> result{};
    if (!owner_array) {
        return result;
    }



    const int count = std::clamp(player_count, 1, MaxRacers);
    std::array<VAddr, MaxRacers> owners{};
    const size_t owner_bytes = static_cast<size_t>(count) * sizeof(VAddr);
    if (!memory.IsValidVirtualAddressRange(owner_array, owner_bytes) ||
        !memory.ReadBlockUnsafe(owner_array, owners.data(), owner_bytes)) {
        return result;
    }
    for (int i = 0; i < count; ++i) {
        const VAddr owner = owners[i];
        if (!owner) {
            continue;
        }
        const auto player = Read<u32>(memory, owner + ItemOwnerPlayerIdOffset);
        if (!player || *player >= MaxRacers || result[*player] != 0) {
            continue;
        }
        result[*player] = owner;
    }
    return result;
}

struct SlotSnapshot {
    s8 item{NoItem};
    u8 count{};
    bool rolling{};
};

SlotSnapshot ReadSlot(Core::Memory::Memory& memory, VAddr slot) {
    if (!slot) {
        return {};
    }
    constexpr VAddr StateStartOffset = 0x40;
    constexpr size_t StateSize =
        ItemSlotCurrentItemOffset - StateStartOffset + sizeof(s32);
    std::array<u8, StateSize> state{};
    const VAddr state_address = slot + StateStartOffset;
    if (!memory.IsValidVirtualAddressRange(state_address, state.size()) ||
        !memory.ReadBlockUnsafe(state_address, state.data(), state.size())) {
        return {};
    }
    const u8 slot_state = state[ItemSlotStateOffset - StateStartOffset];
    s32 stock_item{};
    s32 rolling_item{};
    u32 use_count{};
    u32 stock_count{};
    std::memcpy(&use_count,
                state.data() + (ItemSlotUseCountOffset - StateStartOffset),
                sizeof(use_count));
    std::memcpy(&stock_item,
                state.data() + (ItemSlotStockItemOffset - StateStartOffset),
                sizeof(stock_item));
    std::memcpy(&stock_count,
                state.data() + (ItemSlotStockCountOffset - StateStartOffset),
                sizeof(stock_count));
    std::memcpy(&rolling_item,
                state.data() + (ItemSlotCurrentItemOffset - StateStartOffset),
                sizeof(rolling_item));




    if (slot_state == 1 || slot_state == 2) {
        const int visible_rotation =
            rolling_item >= 0 && rolling_item <= MaxItem ? rolling_item : NoItem;
        return {static_cast<s8>(visible_rotation), 0, true};
    }
    if (slot_state != 3 || stock_item < 0 || stock_item > MaxItem) {
        return {};
    }
    u32 count = use_count;
    if (count == 0) {
        count = stock_count;
    }
    return {static_cast<s8>(stock_item), static_cast<u8>(std::min<u32>(count, 99)), false};
}

int FindLocalIndex(Core::Memory::Memory& memory, VAddr race_info, VAddr holder_array,
                   int player_count) {



    if (holder_array) {
        for (int i = 0; i < player_count; ++i) {
            const auto holder = Read<u64>(memory, holder_array + static_cast<VAddr>(i) * 8);
            const auto vehicle = holder && *holder
                                     ? Read<u64>(memory, *holder + KartHolderVehicleOffset)
                                     : std::nullopt;
            const auto master = vehicle && *vehicle
                                    ? Read<u8>(memory, *vehicle + KartVehicleMasterOffset)
                                    : std::nullopt;
            if (master && *master != 0) {
                const auto player_id = Read<u32>(memory, *vehicle + KartVehiclePlayerIdOffset);
                return player_id && *player_id < static_cast<u32>(player_count)
                           ? static_cast<int>(*player_id)
                           : i;
            }
        }
    }

    for (int i = 0; i < player_count; ++i) {
        const auto player_type = Read<u32>(
            memory, race_info + RaceInfoDriverArrayOffset + i * RaceInfoDriverStride +
                        RaceInfoPlayerTypeOffset);
        if (player_type && *player_type == 0) {
            return i;
        }
    }
    return -1;
}

bool ResolveActiveRace(EmulationSession& session, FrameworkPointers& framework,
                       Core::Memory::Memory*& memory_out, VAddr& main_base_out,
                       int& player_count_out, int& local_index_out, VAddr& owner_array_out) {
    if (!session.IsRunning()) {
        return false;
    }
    auto& system = session.System();
    if (system.GetApplicationProcessProgramID() != TitleId || !IsExpectedBuild(system)) {
        return false;
    }
    const auto [main_base, main_size] = system.GetApplicationProcessMainModule();
    if (!main_base || main_size <= FrameworkRootOffset) {
        return false;
    }
    auto& memory = system.ApplicationMemory();
    const auto resolved = ResolveFramework(memory, main_base);
    if (!resolved || !resolved->scene || !resolved->object_engine || !resolved->race_info) {
        return false;
    }
    const int race_info_count = static_cast<int>(
        Read<u32>(memory, resolved->race_info + RaceInfoPlayerCountOffset).value_or(0));
    if (race_info_count < 1 || race_info_count > MaxRacers) {
        return false;
    }



    const auto race_rule = Read<u32>(memory, resolved->race_info + RaceInfoRuleOffset);
    if (!race_rule || *race_rule > 3) {
        return false;
    }
    const auto race_director =
        Read<u64>(memory, resolved->object_engine + ObjectRaceDirectorOffset);
    if (!race_director || !*race_director) {
        return false;
    }
    const int director_count = static_cast<int>(
        Read<u32>(memory, *race_director + RaceDirectorPlayerCountOffset).value_or(0));
    if (director_count < 1 || director_count > MaxRacers) {
        return false;
    }
    const int player_count = std::min(race_info_count, director_count);
    const VAddr checker_array =
        Read<u64>(memory, *race_director + RaceDirectorCheckersOffset).value_or(0);
    if (!checker_array) {
        return false;
    }
    bool has_live_checker = false;
    for (int i = 0; i < player_count; ++i) {
        const auto checker = Read<u64>(memory, checker_array + static_cast<VAddr>(i) * 8);
        if (checker && *checker) {
            has_live_checker = true;
            break;
        }
    }
    if (!has_live_checker) {
        return false;
    }
    const auto kart_director = Read<u64>(memory, resolved->object_engine + ObjectKartDirectorOffset);
    const VAddr holder_array = kart_director && *kart_director
                                   ? Read<u64>(memory, *kart_director + KartDirectorHoldersOffset)
                                         .value_or(0)
                                   : 0;
    const int local_index = FindLocalIndex(memory, resolved->race_info, holder_array, player_count);
    const auto item_director = Read<u64>(memory, resolved->object_engine + ObjectItemDirectorOffset);
    const auto* process = system.ApplicationProcess();
    const u64 process_id = process ? process->GetProcessId() : 0;
    const VAddr owner_array = item_director && *item_director
                                  ? FindItemOwnerArray(memory, *item_director, player_count,
                                                       process_id)
                                  : 0;
    framework = *resolved;
    memory_out = &memory;
    main_base_out = main_base;
    player_count_out = player_count;
    local_index_out = local_index;
    owner_array_out = owner_array;
    return true;
}

}

int FillSnapshot(EmulationSession& session, void* output, std::size_t output_size) {
    if (!output || output_size < sizeof(SnapshotFrame)) {
        return -static_cast<int>(sizeof(SnapshotFrame));
    }
    SnapshotFrame frame{};
    frame.magic = 0x44384B4D;
    frame.version = 1;
    frame.size = sizeof(SnapshotFrame);
    frame.sequence = Sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    frame.status = static_cast<u32>(Status::NotRunning);
    frame.local_index = -1;
    for (auto& racer : frame.racers) {
        racer.driver_id = -1;
        racer.item0 = NoItem;
        racer.item1 = NoItem;
    }

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
    const auto [published_main_base, published_main_size] =
        system.GetApplicationProcessMainModule();
    const auto published_build_id = system.GetApplicationProcessBuildID();
    if (!published_main_base || !published_main_size ||
        std::all_of(published_build_id.begin(), published_build_id.end(),
                    [](u8 byte) { return byte == 0; })) {



        frame.status = static_cast<u32>(Status::WaitingForRace);
        std::memcpy(output, &frame, sizeof(frame));
        return sizeof(frame);
    }
    if (!IsExpectedBuild(system)) {
        frame.status = static_cast<u32>(Status::UnsupportedBuild);
        std::memcpy(output, &frame, sizeof(frame));
        return sizeof(frame);
    }

    FrameworkPointers framework{};
    Core::Memory::Memory* memory_ptr{};
    VAddr main_base{};
    int player_count{};
    int local_index{-1};
    VAddr owner_array{};
    if (!ResolveActiveRace(session, framework, memory_ptr, main_base, player_count, local_index,
                           owner_array)) {
        frame.status = static_cast<u32>(Status::WaitingForRace);
        std::memcpy(output, &frame, sizeof(frame));
        return sizeof(frame);
    }

    auto& memory = *memory_ptr;
    frame.status = static_cast<u32>(Status::Active);
    frame.player_count = static_cast<u8>(player_count);
    frame.local_index = static_cast<s8>(local_index);




    const u32 raw_course_id =
        Read<u32>(memory, framework.race_info + RaceInfoCourseIdOffset)
            .value_or(std::numeric_limits<u32>::max());
    frame.course_id = raw_course_id <= MaxRawCourseId ? raw_course_id + 1 : 0;

    const auto kart_director = Read<u64>(memory, framework.object_engine + ObjectKartDirectorOffset);
    const VAddr holder_array = kart_director && *kart_director
                                   ? Read<u64>(memory, *kart_director + KartDirectorHoldersOffset)
                                         .value_or(0)
                                   : 0;
    const auto race_director = Read<u64>(memory, framework.object_engine + ObjectRaceDirectorOffset);
    const auto lap_rank_checker = race_director && *race_director
                                      ? Read<u64>(
                                            memory,
                                            *race_director + RaceDirectorLapRankCheckerOffset)
                                      : std::nullopt;
    const u8 lap_count = lap_rank_checker && *lap_rank_checker
                             ? Read<u8>(memory, *lap_rank_checker + LapRankCheckerLapCountOffset)
                                   .value_or(0)
                             : 0;
    frame.lap_total = lap_count >= 1 && lap_count <= 9 ? lap_count : 0;
    const VAddr checker_array = race_director && *race_director
                                    ? Read<u64>(memory, *race_director + RaceDirectorCheckersOffset)
                                          .value_or(0)
                                    : 0;
    const auto owners_by_player = MapOwnersByPlayer(memory, owner_array, player_count);
    std::array<VAddr, MaxRacers> raw_holders{};
    std::array<VAddr, MaxRacers> vehicles{};
    std::array<u32, MaxRacers> live_driver_ids{};
    live_driver_ids.fill(std::numeric_limits<u32>::max());
    std::array<VAddr, MaxRacers> checkers{};
    const size_t pointer_array_bytes = static_cast<size_t>(player_count) * sizeof(VAddr);
    const auto read_pointer_array = [&](VAddr address, auto& destination) {
        return address && memory.IsValidVirtualAddressRange(address, pointer_array_bytes) &&
               memory.ReadBlockUnsafe(address, destination.data(), pointer_array_bytes);
    };
    read_pointer_array(holder_array, raw_holders);
    read_pointer_array(checker_array, checkers);



    for (int holder_index = 0; holder_index < player_count; ++holder_index) {
        const VAddr holder = raw_holders[holder_index];
        const auto vehicle = holder
                                 ? Read<u64>(memory, holder + KartHolderVehicleOffset)
                                 : std::nullopt;
        if (!vehicle || !*vehicle) {
            continue;
        }
        const auto identity =
            Read<std::array<u32, 4>>(memory, *vehicle + KartVehiclePlayerIdOffset);
        const int player_id = identity && (*identity)[0] < static_cast<u32>(player_count)
                                  ? static_cast<int>((*identity)[0])
                                  : holder_index;
        if (!vehicles[player_id]) {
            vehicles[player_id] = *vehicle;
            if (identity) {
                live_driver_ids[player_id] = (*identity)[3];
            }
        }
    }
    constexpr size_t DriverInfoBytes =
        static_cast<size_t>(RaceInfoDriverStride) * MaxRacers;
    std::array<u8, DriverInfoBytes> driver_info{};
    const size_t live_driver_info_bytes =
        static_cast<size_t>(player_count) * RaceInfoDriverStride;
    const VAddr driver_info_address = framework.race_info + RaceInfoDriverArrayOffset;
    const bool have_driver_info =
        memory.IsValidVirtualAddressRange(driver_info_address, live_driver_info_bytes) &&
        memory.ReadBlockUnsafe(driver_info_address, driver_info.data(), live_driver_info_bytes);

    for (int i = 0; i < player_count; ++i) {
        auto& racer = frame.racers[i];
        racer.active = 1;
        racer.local = i == local_index ? 1 : 0;
        u32 driver = std::numeric_limits<u32>::max();
        if (have_driver_info) {
            const size_t driver_base = static_cast<size_t>(i) * RaceInfoDriverStride;
            std::memcpy(&driver, driver_info.data() + driver_base + RaceInfoDriverIdOffset,
                        sizeof(driver));
            racer.driver_variant = driver_info[driver_base + RaceInfoDriverVariantOffset];
        }
        const VAddr vehicle = vehicles[i];




        const u32 live_driver = live_driver_ids[i];
        if (driver > MaxDriverId && vehicle && live_driver <= MaxDriverId) {
            driver = live_driver;
        }
        racer.driver_id = driver <= MaxDriverId
                              ? static_cast<s16>(driver)
                              : -1;

        const auto move = vehicle
                              ? Read<u64>(memory, vehicle + KartVehicleMoveOffset)
                              : std::nullopt;
        const auto position = move && *move
                                  ? Read<u64>(memory, *move + KartMovePositionPointerOffset)
                                  : std::nullopt;
        if (position && *position) {
            std::array<float, 3> xyz{};
            if (memory.IsValidVirtualAddressRange(*position, sizeof(xyz)) &&
                memory.ReadBlockUnsafe(*position, xyz.data(), sizeof(xyz)) &&
                std::all_of(xyz.begin(), xyz.end(), [](float v) { return std::isfinite(v); })) {
                racer.x = xyz[0];
                racer.y = xyz[1];
                racer.z = xyz[2];
            }
        }

        const VAddr checker = checkers[i];
        const auto rank_and_lap = checker
                                      ? Read<std::array<u32, 2>>(
                                            memory, checker + RaceKartCheckerRankOffset)
                                      : std::nullopt;
        const u32 raw_rank = rank_and_lap ? (*rank_and_lap)[0]
                                          : std::numeric_limits<u32>::max();
        racer.rank = raw_rank < static_cast<u32>(player_count)
                         ? static_cast<u8>(raw_rank + 1)
                         : static_cast<u8>(i + 1);
        const u32 completed_laps = rank_and_lap ? (*rank_and_lap)[1]
                                                : std::numeric_limits<u32>::max();
        if (lap_count >= 1 && lap_count <= 9 && completed_laps <= lap_count) {
            racer.lap = static_cast<u8>(
                std::min<u32>(completed_laps + 1, static_cast<u32>(lap_count)));
        }
        racer.coins = checker
                          ? Read<u8>(memory, checker + RaceKartCheckerCoinOffset).value_or(0)
                          : 0;

        if (const VAddr owner = owners_by_player[i]) {
            std::array<VAddr, 2> slots{};
            if (memory.IsValidVirtualAddressRange(owner + ItemOwnerSlot0Offset, sizeof(slots))) {
                memory.ReadBlockUnsafe(owner + ItemOwnerSlot0Offset, slots.data(), sizeof(slots));
            }
            const auto first = ReadSlot(memory, slots[0]);
            const auto second = ReadSlot(memory, slots[1]);
            racer.item0 = first.item;
            racer.item0_count = first.count;
            racer.item1 = second.item;
            racer.item1_count = second.count;
            if (i == local_index) {
                if (first.rolling) {
                    frame.flags |= Slot0RollingFlag;
                }
                if (second.rolling) {
                    frame.flags |= Slot1RollingFlag;
                }
            }
        }
    }

    std::memcpy(output, &frame, sizeof(frame));
    return sizeof(frame);
}

bool ActivateItemSlot(EmulationSession& session, int slot_index) {
    if (slot_index < 0 || slot_index > 1) {
        return false;
    }
    FrameworkPointers framework{};
    Core::Memory::Memory* memory_ptr{};
    VAddr main_base{};
    int player_count{};
    int local_index{-1};
    VAddr owner_array{};
    if (!ResolveActiveRace(session, framework, memory_ptr, main_base, player_count, local_index,
                           owner_array) ||
        local_index < 0 || !owner_array) {
        return false;
    }
    auto& memory = *memory_ptr;
    const VAddr owner = FindOwnerByPlayer(memory, owner_array, player_count, local_index);
    if (!owner) {
        return false;
    }
    const auto target_slot = Read<u64>(
        memory, owner + (slot_index == 0 ? ItemOwnerSlot0Offset : ItemOwnerSlot1Offset));
    if (!target_slot || !*target_slot) {
        return false;
    }
    const auto slot = ReadSlot(memory, *target_slot);
    if (slot.rolling || slot.item == NoItem) {
        return false;
    }




    auto& system = session.System();
    const VAddr mailbox = system.GetApplicationProcessCompanionMailbox();
    if (!IsMailboxAvailable(system, mailbox)) {
        return false;
    }
    const u64 payload = static_cast<u64>(slot_index) + 1;
    std::scoped_lock lock{ActionMutex};
    const u32 state = LoadMailbox<u32>(system, mailbox, MailboxStateOffset, __ATOMIC_ACQUIRE);
    const u64 pending =
        LoadMailbox<u64>(system, mailbox, MailboxRequestOffset, __ATOMIC_ACQUIRE);
    if (state == MailboxQueued || state == MailboxRunning) {


        return false;
    }
    if ((state != MailboxIdle && state != MailboxSucceeded && state != MailboxRejected) ||
        pending != 0) {
        return false;
    }
    StoreMailbox<u64>(system, mailbox, MailboxCompletedOffset, 0, __ATOMIC_RELAXED);
    StoreMailbox<u64>(system, mailbox, MailboxRequestOffset, payload, __ATOMIC_RELAXED);
    StoreMailbox<u32>(system, mailbox, MailboxStateOffset, MailboxQueued, __ATOMIC_RELEASE);
    LOG_INFO(Frontend, "[MK8D Companion] Queued local item slot {} (item {}, count {})",
             slot_index, slot.item, slot.count);
    return true;
}

}
