#include "jni/sshd_companion.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

#include "common/common_types.h"
#include "common/logging.h"
#include "core/core.h"
#include "core/loader/sshd_companion_payload.h"
#include "core/memory.h"
#include "jni/native.h"

namespace SshdCompanion {
namespace {

constexpr u64 TitleId = Loader::SshdCompanionPayload::TitleId;
constexpr auto BuildId = Loader::SshdCompanionPayload::BuildId;

constexpr VAddr FileManagerSaveAOffset = 0x10;
constexpr VAddr SavePouchOffset = 0x07C0;
constexpr VAddr SaveItemCheckOffset = 0x07E0;
constexpr VAddr SaveItemFlagsOffset = 0x09E4;
constexpr VAddr SavePotionTimersOffset = 0x52F4;
constexpr VAddr SaveHealthCapacityOffset = 0x5302;
constexpr VAddr SaveCurrentHealthOffset = 0x5306;
constexpr VAddr SaveAreaOffset = 0x531C;
constexpr VAddr SaveNewFileOffset = 0x53AD;
constexpr VAddr SaveEquippedBItemOffset = 0x53AE;
constexpr VAddr SaveSelectedPouchOffset = 0x53B0;
constexpr VAddr SaveShieldPouchOffset = 0x53B1;
constexpr VAddr SaveDowsingOffset = 0x53B2;
constexpr VAddr PlayerStaminaOffset = 0x64D8;

constexpr u16 MaxDirectItemId = 215;
constexpr u16 ItemDefinitionCount = 0x1FE;
constexpr u16 StoryBossRushActive = 530;
constexpr u16 StoryThunderDragonSongComplete = 21;
constexpr u16 StoryImprisonedFight = 136;
constexpr u16 StoryImprisonedOneFight = 143;
constexpr u16 StoryImprisonedTwoFight = 144;
constexpr u16 StoryImprisonedThreeFight = 145;
constexpr u16 StoryImprisonedThreeDefeated = 133;
constexpr u16 ItemLifeTreeSeedling = 197;
constexpr u16 ItemLifeTreeFruit = 198;

enum class Status : u32 {
    NotRunning = 0,
    WrongTitle = 1,
    UnsupportedBuild = 2,
    Loading = 3,
    Active = 4,
};

enum Capability : u32 {
    SaveState = 1U << 0,
    FlagState = 1U << 1,
    PlayerState = 1U << 2,
    ProgressState = 1U << 3,
};

struct ProgressEntry {
    u16 story_flag;
    u16 message_id;
};

// These are the big-endian navi_table.dat entries, preserved in file order.
constexpr std::array<ProgressEntry, 33> SummaryEntries{{
    {32, 6}, {46, 8}, {87, 9}, {89, 10}, {5, 11}, {183, 12}, {7, 13},
    {211, 14}, {9, 15}, {131, 16}, {71, 17}, {206, 18}, {155, 19}, {11, 20},
    {208, 21}, {156, 22}, {15, 23}, {207, 24}, {173, 25}, {13, 26}, {132, 27},
    {146, 28}, {200, 29}, {16, 30}, {19, 31}, {21, 32}, {133, 38}, {465, 39},
    {369, 33}, {209, 34}, {17, 35}, {134, 36}, {225, 37},
}};

constexpr std::array<ProgressEntry, 59> ObjectiveEntries{{
    {46, 0}, {137, 1}, {87, 2}, {58, 3}, {89, 4}, {83, 97}, {5, 5}, {47, 6},
    {182, 7}, {106, 8}, {183, 9}, {7, 10}, {48, 11}, {190, 12}, {129, 13},
    {211, 14}, {9, 15}, {340, 94}, {131, 16}, {49, 17}, {194, 18}, {71, 19},
    {206, 20}, {63, 21}, {64, 22}, {155, 23}, {11, 24}, {72, 25}, {208, 26},
    {273, 27}, {496, 28}, {497, 29}, {498, 30}, {156, 31}, {15, 32}, {73, 33},
    {207, 34}, {467, 35}, {669, 93}, {186, 36}, {173, 37}, {13, 38}, {144, 39},
    {132, 40}, {350, 41}, {146, 42}, {328, 43}, {642, 44}, {200, 45}, {16, 46},
    {19, 47}, {21, 48}, {465, 49}, {369, 50}, {209, 51}, {22, 95}, {17, 52},
    {134, 53}, {225, 54},
}};

#pragma pack(push, 1)
struct SnapshotFrame {
    u32 magic;
    u16 version;
    u16 size;
    u32 sequence;
    u32 status;
    u32 capabilities;
    u16 current_health;
    u16 health_capacity;
    u32 stamina_amount;
    u16 rupees;
    u16 seeds;
    u16 arrows;
    u16 bombs;
    u16 gratitude_crystals;
    u16 key_pieces;
    u16 heart_pieces;
    u16 pouch_expansions;
    u16 extra_wallets;
    u16 summary_message_id;
    u16 objective_message_id;
    u8 equipped_b_item;
    u8 selected_pouch_slot;
    u8 shield_pouch_slot;
    u8 selected_dowsing_slot;
    u8 current_room;
    u8 current_layer;
    u8 current_entrance;
    u8 current_night;
    u8 current_trial;
    u8 new_file;
    u16 potion_timers[6];
    char stage_name[8];
    char area_name[32];
    s32 pouch_items[8];
    s32 item_check_items[60];
    u16 item_values[MaxDirectItemId + 1];
};
#pragma pack(pop)

static_assert(sizeof(SnapshotFrame) == 816);
std::atomic<u32> Sequence{0};
std::atomic<u64> LoggedRuntimeSignature{~0ULL};

void LogRuntimeStatus(Status status, u32 checkpoint, VAddr main_base, u64 main_size,
                      VAddr file_manager, u16 capacity, u16 health) {
    const u64 signature = (static_cast<u64>(status) << 60) ^
                          (static_cast<u64>(checkpoint) << 52) ^
                          (main_base >> 12) ^ (file_manager << 1) ^
                          (static_cast<u64>(capacity) << 16) ^ health;
    if (LoggedRuntimeSignature.exchange(signature, std::memory_order_relaxed) == signature) {
        return;
    }
    LOG_INFO(Frontend,
             "[SSHD Companion] Runtime status changed: status={} checkpoint={} main={:#x} "
             "size={:#x} file_manager={:#x} capacity={} health={}",
             static_cast<u32>(status), checkpoint, main_base, main_size, file_manager, capacity,
             health);
}

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

bool ReadBytes(Core::Memory::Memory& memory, VAddr address, void* output, std::size_t size) {
    return address && size && memory.IsValidVirtualAddressRange(address, size) &&
           memory.ReadBlockUnsafe(address, output, size);
}

std::optional<Loader::SshdCompanionPayload::Mailbox> ReadPayloadMailbox(
    Core::Memory::Memory& memory, VAddr mailbox) {
    using Payload = Loader::SshdCompanionPayload::Mailbox;
    if (!mailbox || !memory.IsValidVirtualAddressRange(mailbox, sizeof(Payload))) {
        return std::nullopt;
    }

    const auto sequence_before =
        Read<u32>(memory, mailbox + offsetof(Payload, sequence));
    if (!sequence_before || *sequence_before == 0 || (*sequence_before & 1U) != 0) {
        return std::nullopt;
    }
    Payload payload{};
    if (!ReadBytes(memory, mailbox, &payload, sizeof(payload))) {
        return std::nullopt;
    }
    const auto sequence_after =
        Read<u32>(memory, mailbox + offsetof(Payload, sequence));
    if (!sequence_after || *sequence_after != *sequence_before ||
        payload.sequence != *sequence_before ||
        payload.magic != Loader::SshdCompanionPayload::Magic ||
        payload.version != Loader::SshdCompanionPayload::Version ||
        payload.size != sizeof(Payload)) {
        return std::nullopt;
    }
    return payload;
}

struct FlagReader {
    Core::Memory::Memory& memory;
    VAddr data{};
    VAddr definitions{};
    u16 value_count{};
    u16 definition_count{};

    std::optional<u16> Get(u16 id) const {
        if (!data || !definitions || id >= definition_count) {
            return std::nullopt;
        }
        const auto definition = Read<std::array<u8, 2>>(memory, definitions + id * 2);
        if (!definition || (*definition)[0] == 0xFF || (*definition)[0] >= value_count) {
            return std::nullopt;
        }
        const auto packed = Read<u16>(memory, data + static_cast<VAddr>((*definition)[0]) * 2);
        if (!packed) {
            return std::nullopt;
        }
        const u8 shift_mask = (*definition)[1];
        const u8 shift = shift_mask >> 4;
        if (shift >= 16) {
            return std::nullopt;
        }
        const u16 mask = static_cast<u16>((2U << (shift_mask & 0x0F)) - 1U);
        return static_cast<u16>((*packed >> shift) & mask);
    }
};

std::optional<FlagReader> ResolveFlagReader(Core::Memory::Memory& memory, VAddr manager,
                                            u16 expected_values, u16 minimum_definitions) {
    if (!manager) {
        return std::nullopt;
    }
    const auto value_count = Read<u16>(memory, manager + 8);
    const auto flag_space = Read<u64>(memory, manager + 16);
    const auto flag_index = Read<u64>(memory, manager + 24);
    if (!value_count || *value_count != expected_values || !flag_space || !*flag_space ||
        !flag_index || !*flag_index) {
        return std::nullopt;
    }
    const auto data = Read<u64>(memory, *flag_space);
    const auto definition_count = Read<u16>(memory, *flag_index);
    const auto definitions = Read<u64>(memory, *flag_index + 8);
    if (!data || !*data || !definition_count || *definition_count < minimum_definitions ||
        *definition_count > 2048 || !definitions || !*definitions ||
        !memory.IsValidVirtualAddressRange(*data, static_cast<std::size_t>(*value_count) * 2) ||
        !memory.IsValidVirtualAddressRange(*definitions,
                                           static_cast<std::size_t>(*definition_count) * 2)) {
        return std::nullopt;
    }
    return FlagReader{memory, *data, *definitions, *value_count, *definition_count};
}

u16 FindProgressMessage(const FlagReader& story, std::span<const ProgressEntry> entries) {
    for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry) {
        if (story.Get(entry->story_flag).value_or(0) != 0) {
            return entry->message_id;
        }
    }
    return 0xFFFF;
}

void SanitizeAscii(char* value, std::size_t size) {
    bool terminated{};
    for (std::size_t i = 0; i < size; ++i) {
        const auto character = static_cast<unsigned char>(value[i]);
        if (terminated || character == 0) {
            value[i] = 0;
            terminated = true;
        } else if (character < 0x20 || character > 0x7E) {
            value[i] = '?';
        }
    }
    value[size - 1] = 0;
}

} // namespace

int FillSnapshot(EmulationSession& session, void* output, std::size_t output_size) {
    if (!output || output_size < sizeof(SnapshotFrame)) {
        return -1;
    }

    SnapshotFrame frame{};
    frame.magic = 0x44485353; // "SSHD" in a little-endian direct buffer.
    frame.version = 1;
    frame.size = sizeof(frame);
    frame.sequence = Sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    frame.status = static_cast<u32>(Status::NotRunning);
    frame.summary_message_id = 0xFFFF;
    frame.objective_message_id = 0xFFFF;
    std::fill(std::begin(frame.pouch_items), std::end(frame.pouch_items), -1);
    std::fill(std::begin(frame.item_check_items), std::end(frame.item_check_items), -1);

    [[maybe_unused]] auto system_access = session.LockSystemAccess();
    if (!session.IsRunning()) {
        LogRuntimeStatus(Status::NotRunning, 0, 0, 0, 0, 0, 0);
        std::memcpy(output, &frame, sizeof(frame));
        return sizeof(frame);
    }
    auto& system = session.System();
    if (system.GetApplicationProcessProgramID() != TitleId) {
        frame.status = static_cast<u32>(Status::WrongTitle);
        LogRuntimeStatus(Status::WrongTitle, 1, 0, 0, 0, 0, 0);
        std::memcpy(output, &frame, sizeof(frame));
        return sizeof(frame);
    }
    if (system.GetApplicationProcessBuildID() != BuildId) {
        frame.status = static_cast<u32>(Status::UnsupportedBuild);
        LogRuntimeStatus(Status::UnsupportedBuild, 2, 0, 0, 0, 0, 0);
        std::memcpy(output, &frame, sizeof(frame));
        return sizeof(frame);
    }

    const auto [main_base, main_size] = system.GetApplicationProcessMainModule();
    if (!main_base || main_size < 0x01840000) {
        frame.status = static_cast<u32>(Status::Loading);
        frame.capabilities = 3U << 24;
        LogRuntimeStatus(Status::Loading, 3, main_base, main_size, 0, 0, 0);
        std::memcpy(output, &frame, sizeof(frame));
        return sizeof(frame);
    }
    auto& memory = system.ApplicationMemory();
    const VAddr mailbox = system.GetApplicationProcessCompanionMailbox();
    const auto payload = ReadPayloadMailbox(memory, mailbox);
    if (!payload) {
        frame.status = static_cast<u32>(Status::Loading);
        frame.capabilities = 4U << 24;
        LogRuntimeStatus(Status::Loading, 4, main_base, main_size, mailbox, 0, 0);
        std::memcpy(output, &frame, sizeof(frame));
        return sizeof(frame);
    }
    const VAddr file_manager = payload->file_manager;
    const VAddr save = payload->save;
    if (!file_manager || save != file_manager + FileManagerSaveAOffset) {
        frame.status = static_cast<u32>(Status::Loading);
        frame.capabilities = 5U << 24;
        LogRuntimeStatus(Status::Loading, 5, main_base, main_size, file_manager, 0, 0);
        std::memcpy(output, &frame, sizeof(frame));
        return sizeof(frame);
    }
    const auto capacity = Read<u16>(memory, save + SaveHealthCapacityOffset);
    const auto health = Read<u16>(memory, save + SaveCurrentHealthOffset);
    if (!capacity || !health || *capacity == 0 || *capacity > 256 || *health > *capacity) {
        frame.status = static_cast<u32>(Status::Loading);
        frame.capabilities = 5U << 24;
        LogRuntimeStatus(Status::Loading, 5, main_base, main_size, file_manager,
                         capacity.value_or(0), health.value_or(0));
        std::memcpy(output, &frame, sizeof(frame));
        return sizeof(frame);
    }

    frame.current_health = *health;
    frame.health_capacity = *capacity;
    if (!ReadBytes(memory, save + SavePouchOffset, frame.pouch_items,
                   sizeof(frame.pouch_items)) ||
        !ReadBytes(memory, save + SaveItemCheckOffset, frame.item_check_items,
                   sizeof(frame.item_check_items)) ||
        !ReadBytes(memory, save + SavePotionTimersOffset, frame.potion_timers,
                   sizeof(frame.potion_timers)) ||
        !ReadBytes(memory, save + SaveAreaOffset, frame.area_name, sizeof(frame.area_name))) {
        frame.status = static_cast<u32>(Status::Loading);
        frame.capabilities = 6U << 24;
        LogRuntimeStatus(Status::Loading, 6, main_base, main_size, file_manager, *capacity,
                         *health);
        std::memcpy(output, &frame, sizeof(frame));
        return sizeof(frame);
    }
    std::memcpy(frame.stage_name, payload->stage_name.data(), sizeof(frame.stage_name));
    frame.equipped_b_item = Read<u8>(memory, save + SaveEquippedBItemOffset).value_or(0xFF);
    frame.selected_pouch_slot = Read<u8>(memory, save + SaveSelectedPouchOffset).value_or(0xFF);
    frame.shield_pouch_slot = Read<u8>(memory, save + SaveShieldPouchOffset).value_or(0xFF);
    frame.selected_dowsing_slot = Read<u8>(memory, save + SaveDowsingOffset).value_or(0xFF);
    frame.current_room = payload->current_room;
    frame.current_layer = payload->current_layer;
    frame.current_entrance = payload->current_entrance;
    frame.current_night = payload->current_night;
    frame.current_trial = payload->current_trial;
    frame.new_file = Read<u8>(memory, save + SaveNewFileOffset).value_or(0);
    SanitizeAscii(frame.stage_name, sizeof(frame.stage_name));
    SanitizeAscii(frame.area_name, sizeof(frame.area_name));
    frame.capabilities |= SaveState;

    if (payload->player) {
        const auto stamina = Read<u32>(memory, payload->player + PlayerStaminaOffset);
        if (stamina && *stamina <= 100000) {
            frame.stamina_amount = *stamina;
            frame.capabilities |= PlayerState;
        }
    }

    const auto item_flags =
        ResolveFlagReader(memory, payload->item_flag_manager, 0x40, ItemDefinitionCount);
    const auto story_flags = ResolveFlagReader(memory, payload->story_flag_manager, 0x80, 531);
    if (item_flags) {
        for (u16 id = 0; id <= MaxDirectItemId; ++id) {
            frame.item_values[id] = item_flags->Get(id).value_or(0);
        }
        frame.heart_pieces = item_flags->Get(0x1E9).value_or(0);
        frame.pouch_expansions = item_flags->Get(0x1EA).value_or(0);
        frame.seeds = item_flags->Get(0x1ED).value_or(0);
        frame.arrows = item_flags->Get(0x1F2).value_or(0);
        frame.bombs = item_flags->Get(0x1F3).value_or(0);
        frame.rupees = item_flags->Get(0x1F5).value_or(0);
        frame.gratitude_crystals = item_flags->Get(0x1F6).value_or(0);
        frame.key_pieces = item_flags->Get(0x1F9).value_or(0);
        frame.extra_wallets = item_flags->Get(0x1FC).value_or(0);
        frame.capabilities |= FlagState;
    }
    if (story_flags) {
        if (story_flags->Get(StoryBossRushActive).value_or(0) != 0) {
            frame.summary_message_id = 40;
            frame.objective_message_id = 92;
        } else {
            frame.summary_message_id = FindProgressMessage(*story_flags, SummaryEntries);
            const bool imprisoned_repeat =
                story_flags->Get(StoryImprisonedFight).value_or(0) != 0 &&
                story_flags->Get(StoryImprisonedOneFight).value_or(0) != 0 &&
                story_flags->Get(StoryImprisonedTwoFight).value_or(0) != 0 &&
                story_flags->Get(StoryImprisonedThreeFight).value_or(0) != 0 &&
                story_flags->Get(StoryImprisonedThreeDefeated).value_or(0) == 0;
            if (imprisoned_repeat) {
                frame.objective_message_id = 70;
            } else if (story_flags->Get(StoryThunderDragonSongComplete).value_or(0) == 0 &&
                       item_flags && item_flags->Get(ItemLifeTreeSeedling).value_or(0) != 0 &&
                       item_flags->Get(ItemLifeTreeFruit).value_or(0) == 0) {
                frame.objective_message_id = 90;
            } else if (story_flags->Get(StoryThunderDragonSongComplete).value_or(0) == 0 &&
                       item_flags && item_flags->Get(ItemLifeTreeFruit).value_or(0) != 0) {
                frame.objective_message_id = 91;
            } else {
                frame.objective_message_id = FindProgressMessage(*story_flags, ObjectiveEntries);
            }
        }
        frame.capabilities |= ProgressState;
    }

    frame.status = static_cast<u32>(Status::Active);
    LogRuntimeStatus(Status::Active, 7, main_base, main_size, file_manager, *capacity, *health);
    std::memcpy(output, &frame, sizeof(frame));
    return sizeof(frame);
}

} // namespace SshdCompanion
