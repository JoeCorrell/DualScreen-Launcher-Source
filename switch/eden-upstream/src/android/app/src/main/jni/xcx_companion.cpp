// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "jni/xcx_companion.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>

#include "common/common_types.h"
#include "common/logging.h"
#include "core/core.h"
#include "core/hle/kernel/k_process.h"
#include "core/loader/xcx_companion_payload.h"
#include "core/memory.h"
#include "jni/native.h"

namespace XcxCompanion {
namespace {

constexpr u64 TitleId = XcxCompanionPayload::TitleId;
constexpr auto BuildId = XcxCompanionPayload::BuildId;

constexpr u32 Magic = 0x53584358; // "XCXS" in a little-endian direct buffer.
constexpr u16 Version = 4;
constexpr std::size_t SegmentScoreCount = 6;
constexpr std::size_t PartyMemberCount = 4;

// Verified against the exact 1.0.2 main executable above. These are centralized so an
// additional game version can never accidentally combine offsets from different builds.
constexpr VAddr GameStatePointerOffset = 0x01DC1408;
constexpr VAddr ControlledActorSlotsOffset = 0x01D55110;
constexpr VAddr HandlePoolOffset = 0x01D55130;
constexpr VAddr BattleRegistryOffset = 0x01D557A8;
constexpr VAddr ZoneManagerGotOffset = 0x01D55148;
constexpr VAddr QuestStateGotOffset = 0x01D55638;
constexpr VAddr SegmentStateGotOffset = 0x01D58F60;
constexpr VAddr FlagManagerGotOffset = 0x01D55E40;
constexpr VAddr PauseManagerGotOffset = 0x01D54EC0;
constexpr VAddr SelectedActorRootOffset = 0x026D7228;

constexpr VAddr GameStateMiraniumOffset = 0x001B5D0;
constexpr VAddr GameStatePartyOffset = 0x001B5DC;
constexpr VAddr PartyRecordStride = 8;
constexpr VAddr ControlledActorSlotStride = 0x14;
constexpr VAddr ZoneManagerZoneOffset = 0x00AC;
constexpr VAddr QuestLastClearedIdOffset = 0x0089F0;
constexpr VAddr SegmentEntryStride = 0x2510;
constexpr VAddr SegmentScoreOffset = 0x28;
constexpr u32 MaximumMiranium = 99'999'999;
constexpr u32 MaximumZoneId = 65'535;
constexpr u32 MaximumSegmentScore = 100'000'000;

enum class Status : u32 {
    NotRunning = 0,
    WrongTitle = 1,
    UnsupportedBuild = 2,
    Loading = 3,
    Active = 4,
};

enum Capability : u32 {
    GameState = 1U << 0,
    ZoneState = 1U << 1,
    QuestState = 1U << 2,
    SegmentState = 1U << 3,
    PlayerTransform = 1U << 4,
    PartyState = 1U << 5,
    CombatState = 1U << 6,
    ActionBridge = 1U << 7,
};

#pragma pack(push, 1)
struct SnapshotFrame {
    u32 magic;
    u16 version;
    u16 size;
    u32 sequence;
    u32 status;
    u32 capabilities;
    u32 checkpoint;
    u32 zone;
    u32 miranium;
    s32 last_cleared_quest_id;
    u32 valid_segment_mask;
    u32 segment_scores[SegmentScoreCount];
    u32 player_transform_valid;
    float player_x;
    float player_y;
    float player_z;
    float player_rotation_x;
    float player_rotation_y;
    float player_rotation_z;
    float player_rotation_w;
    u32 party_valid_mask;
    u32 party_handles[PartyMemberCount];
    u16 party_character_ids[PartyMemberCount];
    u16 party_actor_slots[PartyMemberCount];
    u8 party_record_states[PartyMemberCount];
    u32 party_record_flags[PartyMemberCount];
    s32 active_party_slot;
    u16 player_character_id;
    u16 player_actor_type;
    u32 battle_enemy_count;
    u32 pause_flags;
    u32 action_state;
    u32 action_type;
    u32 action_argument;
    u32 action_result;
    u64 action_request;
    u64 action_completed;
    u64 sample_time_us;
};
#pragma pack(pop)

static_assert(sizeof(SnapshotFrame) == 208);

std::atomic<u32> Sequence{0};
std::atomic<u64> LoggedRuntimeSignature{~0ULL};
std::atomic<u64> LoggedPlayerProbeSignature{~0ULL};
std::atomic<u64> TotalSampleTimeUs{0};
std::atomic<u64> MaximumSampleTimeUs{0};
std::atomic<u32> PerformanceSampleCount{0};
std::atomic<u64> ActionSequence{0};
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

std::optional<VAddr> ResolveGotPointer(Core::Memory::Memory& memory, VAddr cell_address) {
    const auto global_address = Read<u64>(memory, cell_address);
    if (!global_address || !*global_address) {
        return std::nullopt;
    }
    const auto object = Read<u64>(memory, *global_address);
    if (!object || !*object) {
        return std::nullopt;
    }
    return *object;
}

struct PlayerProbe {
    u32 handle{};
    VAddr handle_object{};
    VAddr actor_component{};
    VAddr actor_vtable{};
    VAddr actor_resolver{};
    VAddr entity{};
    VAddr transform_manager{};
    VAddr transform_state{};
    VAddr root_transform{};
};

std::optional<VAddr> ResolveHandleObject(Core::Memory::Memory& memory, VAddr pool,
                                         u32 handle) {
    constexpr u32 HandleTokenMask = 0x0007FFFF;
    constexpr VAddr SlotStride = 0x18;
    constexpr VAddr OverflowSlotBase = 0x1A038;
    const u32 token = handle & HandleTokenMask;
    if (!pool || token == 0) {
        return std::nullopt;
    }

    const bool overflow = (handle & 0x80000000U) != 0;
    const u32 index = overflow ? ((handle >> 19) & 0x0FFFU) : (handle >> 19);
    // Both branches address the object-pointer field first. The normal pool stores it at +0x10
    // in each 0x18-byte entry; the overflow pool's equivalent field begins at +0x1A038.
    // The generation token immediately follows the pointer in either layout.
    const VAddr slot = pool + static_cast<VAddr>(index) * SlotStride +
                       (overflow ? OverflowSlotBase : 0x10);
    const auto stored_token = Read<u32>(memory, slot + 0x10);
    const auto object = Read<u64>(memory, slot);
    if (!stored_token || *stored_token != token || !object || !*object) {
        return std::nullopt;
    }
    return *object;
}

u32 ReadControlledActorHandle(Core::Memory::Memory& memory, VAddr slot) {
    const bool use_secondary = Read<u8>(memory, slot + 0x16).value_or(0) != 0;
    return Read<u32>(memory, slot + (use_secondary ? 0x10 : 0x0C)).value_or(0);
}

s32 FindActivePartySlot(Core::Memory::Memory& memory, VAddr actor_slots) {
    if (!actor_slots) {
        return -1;
    }
    for (std::size_t index = 0; index < PartyMemberCount; ++index) {
        if (Read<u8>(memory, actor_slots + index * ControlledActorSlotStride + 0x15)
                .value_or(0) != 0) {
            return static_cast<s32>(index);
        }
    }
    return -1;
}

PlayerProbe ProbePlayerObject(Core::Memory::Memory& memory, VAddr main_base,
                              s32 active_party_slot) {
    PlayerProbe probe{};
    const VAddr actor_slots =
        Read<u64>(memory, main_base + ControlledActorSlotsOffset).value_or(0);
    const VAddr handle_pool = Read<u64>(memory, main_base + HandlePoolOffset).value_or(0);
    if (!actor_slots || !handle_pool) {
        return probe;
    }

    if (active_party_slot < 0 || active_party_slot >= static_cast<s32>(PartyMemberCount)) {
        return probe;
    }
    const VAddr active_slot =
        actor_slots + static_cast<VAddr>(active_party_slot) * ControlledActorSlotStride;
    probe.handle = ReadControlledActorHandle(memory, active_slot);
    probe.handle_object = ResolveHandleObject(memory, handle_pool, probe.handle).value_or(0);
    if (!probe.handle_object) {
        return probe;
    }

    probe.actor_component = Read<u64>(memory, probe.handle_object + 0x200).value_or(0);
    probe.actor_vtable = Read<u64>(memory, probe.actor_component).value_or(0);
    probe.actor_resolver = Read<u64>(memory, probe.actor_vtable + 0x78).value_or(0);
    probe.entity = Read<u64>(memory, probe.handle_object + 0x1F0).value_or(0);
    probe.transform_manager = Read<u64>(memory, probe.entity + 0x1AC0).value_or(0);
    probe.transform_state = Read<u64>(memory, probe.transform_manager + 0xC8).value_or(0);
    probe.root_transform = Read<u64>(memory, probe.transform_state + 0x50).value_or(0);
    return probe;
}

void LogPlayerProbe(const PlayerProbe& probe, VAddr main_base) {
    const u64 signature = (static_cast<u64>(probe.handle) << 32) ^ probe.handle_object ^
                          probe.actor_component ^ probe.actor_resolver ^ probe.root_transform;
    if (LoggedPlayerProbeSignature.exchange(signature, std::memory_order_relaxed) == signature) {
        return;
    }
    const u64 resolver_offset = probe.actor_resolver >= main_base
                                    ? probe.actor_resolver - main_base
                                    : 0;
    LOG_INFO(Frontend,
             "[XCX Companion] Player probe: handle={:#x} handle_object={:#x} "
             "component={:#x} vtable={:#x} resolver={:#x} resolver_main_offset={:#x} "
             "entity={:#x} transform_manager={:#x} transform_state={:#x} root={:#x}",
             probe.handle, probe.handle_object, probe.actor_component, probe.actor_vtable,
             probe.actor_resolver, resolver_offset, probe.entity, probe.transform_manager,
             probe.transform_state, probe.root_transform);
}

bool ReadPlayerTransform(Core::Memory::Memory& memory, const PlayerProbe& probe,
                         SnapshotFrame& frame) {
    if (!probe.root_transform) {
        return false;
    }
    const auto x = Read<float>(memory, probe.root_transform + 0x00);
    const auto y = Read<float>(memory, probe.root_transform + 0x04);
    const auto z = Read<float>(memory, probe.root_transform + 0x08);
    const auto rotation_x = Read<float>(memory, probe.root_transform + 0x10);
    const auto rotation_y = Read<float>(memory, probe.root_transform + 0x14);
    const auto rotation_z = Read<float>(memory, probe.root_transform + 0x18);
    const auto rotation_w = Read<float>(memory, probe.root_transform + 0x1C);
    if (!x || !y || !z || !rotation_x || !rotation_y || !rotation_z || !rotation_w ||
        !std::isfinite(*x) || !std::isfinite(*y) || !std::isfinite(*z) ||
        !std::isfinite(*rotation_x) || !std::isfinite(*rotation_y) ||
        !std::isfinite(*rotation_z) || !std::isfinite(*rotation_w) ||
        std::abs(*x) > 10'000'000.0F || std::abs(*y) > 10'000'000.0F ||
        std::abs(*z) > 10'000'000.0F) {
        return false;
    }
    const float rotation_length_squared = *rotation_x * *rotation_x + *rotation_y * *rotation_y +
                                          *rotation_z * *rotation_z + *rotation_w * *rotation_w;
    if (!std::isfinite(rotation_length_squared) || rotation_length_squared < 0.25F ||
        rotation_length_squared > 4.0F) {
        return false;
    }
    const float inverse_rotation_length = 1.0F / std::sqrt(rotation_length_squared);
    frame.player_x = *x;
    frame.player_y = *y;
    frame.player_z = *z;
    frame.player_rotation_x = *rotation_x * inverse_rotation_length;
    frame.player_rotation_y = *rotation_y * inverse_rotation_length;
    frame.player_rotation_z = *rotation_z * inverse_rotation_length;
    frame.player_rotation_w = *rotation_w * inverse_rotation_length;
    frame.player_transform_valid = 1;
    frame.capabilities |= PlayerTransform;
    return true;
}

void ReadPartyState(Core::Memory::Memory& memory, VAddr main_base, VAddr game_state,
                    SnapshotFrame& frame) {
    const VAddr actor_slots =
        Read<u64>(memory, main_base + ControlledActorSlotsOffset).value_or(0);
    const VAddr handle_pool = Read<u64>(memory, main_base + HandlePoolOffset).value_or(0);
    frame.active_party_slot = FindActivePartySlot(memory, actor_slots);
    if (!game_state) {
        return;
    }

    for (std::size_t index = 0; index < PartyMemberCount; ++index) {
        const VAddr record = game_state + GameStatePartyOffset + index * PartyRecordStride;
        const u8 record_state = Read<u8>(memory, record).value_or(0);
        const u16 character_id = Read<u8>(memory, record + 1).value_or(0);
        const u32 record_flags = Read<u32>(memory, record + 4).value_or(0);
        frame.party_record_states[index] = record_state;
        frame.party_record_flags[index] = record_flags;
        frame.party_character_ids[index] = character_id;
        if (character_id == 0 || character_id > 23) {
            continue;
        }

        frame.party_valid_mask |= 1U << index;
        if (actor_slots && handle_pool) {
            const VAddr slot = actor_slots + index * ControlledActorSlotStride;
            const u32 handle = ReadControlledActorHandle(memory, slot);
            const VAddr object = ResolveHandleObject(memory, handle_pool, handle).value_or(0);
            if (object) {
                const u16 object_character_id = Read<u16>(memory, object + 0x28C).value_or(0);
                const u16 actor_party_slot = Read<u16>(memory, object + 0x28E).value_or(0xFFFF);
                if (object_character_id == character_id) {
                    frame.party_handles[index] = handle;
                    frame.party_actor_slots[index] = actor_party_slot;
                }
                if (static_cast<s32>(index) == frame.active_party_slot) {
                    frame.player_actor_type = Read<u8>(memory, object + 0xB4).value_or(0);
                    frame.player_character_id = object_character_id;
                }
            }
        }
        if (static_cast<s32>(index) == frame.active_party_slot &&
            frame.player_character_id == 0) {
            frame.player_character_id = character_id;
        }
    }
    if (frame.party_valid_mask != 0) {
        frame.capabilities |= PartyState;
    }
}

void ReadCombatState(Core::Memory::Memory& memory, VAddr main_base, SnapshotFrame& frame) {
    const VAddr registry = Read<u64>(memory, main_base + BattleRegistryOffset).value_or(0);
    const VAddr handle_pool = Read<u64>(memory, main_base + HandlePoolOffset).value_or(0);
    if (!registry || !handle_pool) {
        return;
    }

    // GetEnemyNumInBattle (main+0x3D3750) walks the same 32 registry buckets with mask 0x20.
    // Reproduce only its read-only collection/counting portion here. Every list link and actor
    // handle is range checked and traversal is bounded so a loading transition cannot loop.
    u32 count = 0;
    u32 traversed = 0;
    constexpr u32 MaximumBattleNodes = 256;
    for (std::size_t bucket = 0; bucket < 32 && traversed < MaximumBattleNodes; ++bucket) {
        const VAddr entry = Read<u64>(memory, registry + 0x70 + bucket * sizeof(u64)).value_or(0);
        if (!entry || (Read<u32>(memory, entry + 0x84).value_or(0) & 0x20U) == 0) {
            continue;
        }
        const VAddr sentinel = Read<u64>(memory, entry + 0x20).value_or(0);
        VAddr node = sentinel ? Read<u64>(memory, sentinel).value_or(0) : 0;
        while (node && node != sentinel && traversed++ < MaximumBattleNodes) {
            const u32 handle = Read<u32>(memory, node + 0x10).value_or(0);
            const VAddr object = ResolveHandleObject(memory, handle_pool, handle).value_or(0);
            const VAddr component = object ? Read<u64>(memory, object + 0x200).value_or(0) : 0;
            const u16 flags = object ? Read<u16>(memory, object + 0xB6).value_or(0) : 0;
            if (component && (flags & 0x20U) != 0) {
                ++count;
            }
            node = Read<u64>(memory, node).value_or(0);
        }
    }
    frame.battle_enemy_count = std::min(count, MaximumBattleNodes);
    frame.capabilities |= CombatState;
}

void ReadPauseState(Core::Memory::Memory& memory, VAddr main_base, SnapshotFrame& frame) {
    const VAddr pause_root = Read<u64>(memory, main_base + PauseManagerGotOffset).value_or(0);
    const VAddr pause_manager = pause_root ? Read<u64>(memory, pause_root).value_or(0) : 0;
    if (pause_manager) {
        frame.pause_flags = Read<u8>(memory, pause_manager + 0xB0).value_or(0);
    }
}

bool IsActionMailboxAvailable(Core::System& system, VAddr mailbox) {
    using Payload = XcxCompanionPayload::Mailbox;
    auto& memory = system.ApplicationMemory();
    return mailbox && (mailbox & 7U) == 0 &&
           memory.IsValidVirtualAddressRange(mailbox, sizeof(Payload)) &&
           memory.GetPointerSilent(mailbox) != nullptr &&
           memory.GetPointerSilent(mailbox + sizeof(Payload) - 1) != nullptr;
}

template <typename T>
T LoadMailbox(Core::System& system, VAddr mailbox, std::size_t offset, int ordering) {
    const auto* pointer = system.ApplicationMemory().GetPointer<T>(mailbox + offset);
    return pointer ? __atomic_load_n(pointer, ordering) : T{};
}

template <typename T>
bool StoreMailbox(Core::System& system, VAddr mailbox, std::size_t offset, T value,
                  int ordering) {
    auto* pointer = system.ApplicationMemory().GetPointer<T>(mailbox + offset);
    if (!pointer) {
        return false;
    }
    __atomic_store_n(pointer, value, ordering);
    return true;
}

void ReadActionState(Core::System& system, SnapshotFrame& frame) {
    using Payload = XcxCompanionPayload::Mailbox;
    const VAddr mailbox = system.GetApplicationProcessCompanionMailbox();
    if (!IsActionMailboxAvailable(system, mailbox) ||
        LoadMailbox<u32>(system, mailbox, offsetof(Payload, magic), __ATOMIC_ACQUIRE) !=
            XcxCompanionPayload::Magic ||
        LoadMailbox<u16>(system, mailbox, offsetof(Payload, version), __ATOMIC_RELAXED) !=
            XcxCompanionPayload::Version ||
        LoadMailbox<u16>(system, mailbox, offsetof(Payload, size), __ATOMIC_RELAXED) !=
            sizeof(Payload)) {
        return;
    }
    frame.action_state =
        LoadMailbox<u32>(system, mailbox, offsetof(Payload, state), __ATOMIC_ACQUIRE);
    frame.action_type =
        LoadMailbox<u32>(system, mailbox, offsetof(Payload, action), __ATOMIC_RELAXED);
    frame.action_argument =
        LoadMailbox<u32>(system, mailbox, offsetof(Payload, argument), __ATOMIC_RELAXED);
    frame.action_result =
        LoadMailbox<u32>(system, mailbox, offsetof(Payload, result), __ATOMIC_RELAXED);
    frame.action_request =
        LoadMailbox<u64>(system, mailbox, offsetof(Payload, request), __ATOMIC_ACQUIRE);
    frame.action_completed =
        LoadMailbox<u64>(system, mailbox, offsetof(Payload, completed), __ATOMIC_ACQUIRE);
    frame.capabilities |= ActionBridge;
}

void LogRuntimeStatus(Core::System* system, Status status, u32 checkpoint, VAddr main_base,
                      u64 main_size, const SnapshotFrame& frame, VAddr game_state,
                      VAddr zone_manager, VAddr quest_state, VAddr segment_state) {
    const auto* process = system ? system->ApplicationProcess() : nullptr;
    const u64 process_id = process ? process->GetProcessId() : 0;
    const u64 signature = (process_id << 32) ^ (static_cast<u64>(status) << 28) ^
                          (static_cast<u64>(checkpoint) << 20) ^ frame.capabilities ^
                          (static_cast<u64>(frame.zone) << 4);
    if (LoggedRuntimeSignature.exchange(signature, std::memory_order_relaxed) == signature) {
        return;
    }
    LOG_INFO(Frontend,
             "[XCX Companion] Runtime status changed: pid={} status={} checkpoint={} "
             "main={:#x} size={:#x} capabilities={:#x} zone={} miranium={} "
             "last_quest={} game_state={:#x} zone_manager={:#x} quest_state={:#x} "
             "segment_state={:#x}",
             process_id, static_cast<u32>(status), checkpoint, main_base, main_size,
             frame.capabilities, frame.zone, frame.miranium, frame.last_cleared_quest_id,
             game_state, zone_manager, quest_state, segment_state);
}

void RecordPerformance(u64 elapsed_us) {
    TotalSampleTimeUs.fetch_add(elapsed_us, std::memory_order_relaxed);
    u64 current_max = MaximumSampleTimeUs.load(std::memory_order_relaxed);
    while (elapsed_us > current_max &&
           !MaximumSampleTimeUs.compare_exchange_weak(current_max, elapsed_us,
                                                       std::memory_order_relaxed)) {
    }
    const u32 count = PerformanceSampleCount.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count < 120 || count % 120 != 0) {
        return;
    }
    const u64 total = TotalSampleTimeUs.exchange(0, std::memory_order_relaxed);
    const u64 maximum = MaximumSampleTimeUs.exchange(0, std::memory_order_relaxed);
    PerformanceSampleCount.store(0, std::memory_order_relaxed);
    LOG_INFO(Frontend, "[XCX-PERF] snapshot samples={} average_us={} maximum_us={}", count,
             total / count, maximum);
}

} // namespace

int FillSnapshot(EmulationSession& session, void* output, std::size_t output_size) {
    const auto started = std::chrono::steady_clock::now();
    if (!output || output_size < sizeof(SnapshotFrame)) {
        return -static_cast<int>(sizeof(SnapshotFrame));
    }

    SnapshotFrame frame{};
    frame.magic = Magic;
    frame.version = Version;
    frame.size = sizeof(frame);
    frame.sequence = Sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    frame.status = static_cast<u32>(Status::NotRunning);
    frame.last_cleared_quest_id = -1;
    frame.active_party_slot = -1;
    for (auto& actor_slot : frame.party_actor_slots) {
        actor_slot = 0xFFFF;
    }

    auto finish = [&](Core::System* system, Status status, u32 checkpoint, VAddr main_base,
                      u64 main_size, VAddr game_state, VAddr zone_manager, VAddr quest_state,
                      VAddr segment_state) {
        frame.status = static_cast<u32>(status);
        frame.checkpoint = checkpoint;
        const auto finished = std::chrono::steady_clock::now();
        frame.sample_time_us = static_cast<u64>(
            std::chrono::duration_cast<std::chrono::microseconds>(finished - started).count());
        LogRuntimeStatus(system, status, checkpoint, main_base, main_size, frame, game_state,
                         zone_manager, quest_state, segment_state);
        RecordPerformance(frame.sample_time_us);
        std::memcpy(output, &frame, sizeof(frame));
        return static_cast<int>(sizeof(frame));
    };

    [[maybe_unused]] auto system_access = session.LockSystemAccess();
    if (!session.IsRunning()) {
        return finish(nullptr, Status::NotRunning, 0, 0, 0, 0, 0, 0, 0);
    }

    auto& system = session.System();
    if (system.GetApplicationProcessProgramID() != TitleId) {
        return finish(&system, Status::WrongTitle, 1, 0, 0, 0, 0, 0, 0);
    }

    const auto [main_base, main_size] = system.GetApplicationProcessMainModule();
    if (!main_base || main_size <= GameStatePointerOffset + sizeof(u64)) {
        return finish(&system, Status::Loading, 2, main_base, main_size, 0, 0, 0, 0);
    }
    if (system.GetApplicationProcessBuildID() != BuildId) {
        return finish(&system, Status::UnsupportedBuild, 3, main_base, main_size, 0, 0, 0, 0);
    }

    auto& memory = system.ApplicationMemory();
    const VAddr game_state = Read<u64>(memory, main_base + GameStatePointerOffset).value_or(0);
    const VAddr actor_slots =
        Read<u64>(memory, main_base + ControlledActorSlotsOffset).value_or(0);
    const s32 active_party_slot = FindActivePartySlot(memory, actor_slots);
    const PlayerProbe player_probe = ProbePlayerObject(memory, main_base, active_party_slot);
    LogPlayerProbe(player_probe, main_base);
    ReadPlayerTransform(memory, player_probe, frame);
    ReadPartyState(memory, main_base, game_state, frame);
    ReadCombatState(memory, main_base, frame);
    ReadPauseState(memory, main_base, frame);
    ReadActionState(system, frame);
    const VAddr zone_manager =
        ResolveGotPointer(memory, main_base + ZoneManagerGotOffset).value_or(0);
    const VAddr quest_state =
        ResolveGotPointer(memory, main_base + QuestStateGotOffset).value_or(0);
    const VAddr segment_state =
        ResolveGotPointer(memory, main_base + SegmentStateGotOffset).value_or(0);

    if (game_state) {
        const auto miranium = Read<u32>(memory, game_state + GameStateMiraniumOffset);
        if (miranium && *miranium <= MaximumMiranium) {
            frame.miranium = *miranium;
            frame.capabilities |= GameState;
        }
    }

    if (zone_manager) {
        const auto zone = Read<u32>(memory, zone_manager + ZoneManagerZoneOffset);
        if (zone && *zone <= MaximumZoneId) {
            frame.zone = *zone;
            frame.capabilities |= ZoneState;
        }
    }

    if (quest_state) {
        const auto quest = Read<s32>(memory, quest_state + QuestLastClearedIdOffset);
        if (quest && *quest >= -1 && *quest <= 10'000'000) {
            frame.last_cleared_quest_id = *quest;
            frame.capabilities |= QuestState;
        }
    }

    if (segment_state) {
        for (std::size_t index = 0; index < SegmentScoreCount; ++index) {
            const auto score = Read<u32>(memory, segment_state + index * SegmentEntryStride +
                                                    SegmentScoreOffset);
            if (score && *score <= MaximumSegmentScore) {
                frame.segment_scores[index] = *score;
                frame.valid_segment_mask |= 1U << index;
            }
        }
        if (frame.valid_segment_mask != 0) {
            frame.capabilities |= SegmentState;
        }
    }

    const Status status =
        (frame.capabilities & ~static_cast<u32>(ActionBridge)) != 0 ? Status::Active
                                                                    : Status::Loading;
    const u32 checkpoint = status == Status::Active ? 5 : 4;
    return finish(&system, status, checkpoint, main_base, main_size, game_state, zone_manager,
                  quest_state, segment_state);
}

bool PerformAction(EmulationSession& session, int action, int argument) {
    using Payload = XcxCompanionPayload::Mailbox;
    using Action = XcxCompanionPayload::Action;
    using State = XcxCompanionPayload::State;
    const auto requested_action = static_cast<Action>(action);
    if ((requested_action != Action::SwitchCharacter && requested_action != Action::FastTravel) ||
        argument <= 0) {
        return false;
    }

    std::scoped_lock action_lock{ActionMutex};
    [[maybe_unused]] auto system_access = session.LockSystemAccess();
    if (!session.IsRunning()) {
        return false;
    }
    auto& system = session.System();
    if (system.GetApplicationProcessProgramID() != TitleId ||
        system.GetApplicationProcessBuildID() != BuildId) {
        return false;
    }
    const auto [main_base, main_size] = system.GetApplicationProcessMainModule();
    if (!main_base || main_size <= BattleRegistryOffset + sizeof(u64)) {
        return false;
    }

    auto& memory = system.ApplicationMemory();
    SnapshotFrame live{};
    ReadCombatState(memory, main_base, live);
    ReadPauseState(memory, main_base, live);
    if ((live.capabilities & CombatState) == 0 || live.battle_enemy_count != 0 ||
        live.pause_flags != 0) {
        return false;
    }

    if (requested_action == Action::SwitchCharacter) {
        if (argument > 23) {
            return false;
        }
        const VAddr game_state =
            Read<u64>(memory, main_base + GameStatePointerOffset).value_or(0);
        const VAddr actor_slots =
            Read<u64>(memory, main_base + ControlledActorSlotsOffset).value_or(0);
        const VAddr selected_actor_owner =
            Read<u64>(memory, main_base + SelectedActorRootOffset).value_or(0);
        const VAddr selected_actor = selected_actor_owner
                                         ? Read<u64>(memory, selected_actor_owner + 0x630)
                                               .value_or(0)
                                         : 0;
        const s32 active_slot = FindActivePartySlot(memory, actor_slots);
        bool member_present{};
        u16 active_character{};
        if (!game_state || !selected_actor || active_slot < 0 ||
            Read<u8>(memory, selected_actor + 0xB4).value_or(0) == 2) {
            return false;
        }
        for (std::size_t index = 0; index < PartyMemberCount; ++index) {
            const VAddr record = game_state + GameStatePartyOffset + index * PartyRecordStride;
            const u16 character_id = Read<u8>(memory, record + 1).value_or(0);
            member_present |= character_id == static_cast<u16>(argument);
            if (static_cast<s32>(index) == active_slot) {
                active_character = character_id;
            }
        }
        if (!member_present || active_character == 0 || active_character == argument) {
            return false;
        }
    } else if (argument >= 2048) {
        // The exact 1.0.2 FLD_FormationList contains IDs 1..1441. Keep a small bounded margin
        // while refusing arbitrary indices before they reach the native formation loader.
        return false;
    }

    const VAddr mailbox = system.GetApplicationProcessCompanionMailbox();
    if (!IsActionMailboxAvailable(system, mailbox) ||
        LoadMailbox<u32>(system, mailbox, offsetof(Payload, magic), __ATOMIC_ACQUIRE) !=
            XcxCompanionPayload::Magic ||
        LoadMailbox<u16>(system, mailbox, offsetof(Payload, version), __ATOMIC_RELAXED) !=
            XcxCompanionPayload::Version ||
        LoadMailbox<u16>(system, mailbox, offsetof(Payload, size), __ATOMIC_RELAXED) !=
            sizeof(Payload)) {
        return false;
    }
    const auto state = static_cast<State>(
        LoadMailbox<u32>(system, mailbox, offsetof(Payload, state), __ATOMIC_ACQUIRE));
    const u64 pending =
        LoadMailbox<u64>(system, mailbox, offsetof(Payload, request), __ATOMIC_ACQUIRE);
    if ((state != State::Idle && state != State::Succeeded && state != State::Rejected) ||
        pending != 0) {
        return false;
    }
    const u64 completed =
        LoadMailbox<u64>(system, mailbox, offsetof(Payload, completed), __ATOMIC_ACQUIRE);
    u64 sequence = ActionSequence.fetch_add(1, std::memory_order_relaxed) + 1;
    if (sequence <= completed) {
        sequence = completed + 1;
        ActionSequence.store(sequence, std::memory_order_relaxed);
    }
    if (!StoreMailbox<u32>(system, mailbox, offsetof(Payload, action),
                           static_cast<u32>(requested_action), __ATOMIC_RELAXED) ||
        !StoreMailbox<u32>(system, mailbox, offsetof(Payload, argument),
                           static_cast<u32>(argument), __ATOMIC_RELAXED) ||
        !StoreMailbox<u32>(system, mailbox, offsetof(Payload, result),
                           static_cast<u32>(XcxCompanionPayload::Result::None),
                           __ATOMIC_RELAXED) ||
        !StoreMailbox<u32>(system, mailbox, offsetof(Payload, native_result), 0U,
                           __ATOMIC_RELAXED) ||
        !StoreMailbox<u64>(system, mailbox, offsetof(Payload, request), sequence,
                           __ATOMIC_RELAXED) ||
        !StoreMailbox<u32>(system, mailbox, offsetof(Payload, state),
                           static_cast<u32>(State::Queued), __ATOMIC_RELEASE)) {
        return false;
    }
    LOG_INFO(Frontend, "[XCX Companion] Queued native action {} argument {} sequence {}",
             action, argument, sequence);
    return true;
}

int ReadGameFlags(EmulationSession& session, unsigned int bit_width,
                  const unsigned int* ids, std::size_t count, unsigned char* output,
                  std::size_t output_size) {
    if (!ids || !output || count == 0 || count > 4096 || output_size < count ||
        (bit_width != 1 && bit_width != 2 && bit_width != 4 && bit_width != 8)) {
        return -1;
    }
    [[maybe_unused]] auto system_access = session.LockSystemAccess();
    if (!session.IsRunning()) {
        return 0;
    }
    auto& system = session.System();
    if (system.GetApplicationProcessProgramID() != TitleId ||
        system.GetApplicationProcessBuildID() != BuildId) {
        return 0;
    }
    const auto [main_base, main_size] = system.GetApplicationProcessMainModule();
    if (!main_base || main_size <= FlagManagerGotOffset + sizeof(u64)) {
        return 0;
    }
    auto& memory = system.ApplicationMemory();
    const VAddr global = Read<u64>(memory, main_base + FlagManagerGotOffset).value_or(0);
    const VAddr manager = global ? Read<u64>(memory, global).value_or(0) : 0;
    const VAddr flag_object = manager ? Read<u64>(memory, manager + 8).value_or(0) : 0;
    const VAddr storage = flag_object ? Read<u64>(memory, flag_object).value_or(0) : 0;
    if (!storage) {
        return 0;
    }
    VAddr bank_offset{};
    u32 maximum_id{};
    switch (bit_width) {
    case 1:
        bank_offset = 0x0000;
        maximum_id = 0x10000 - 1;
        break;
    case 2:
        bank_offset = 0x2000;
        maximum_id = 0x2000 - 1;
        break;
    case 4:
        bank_offset = 0x2800;
        maximum_id = 0x2000 - 1;
        break;
    case 8:
        bank_offset = 0x3800;
        maximum_id = 0x2000 - 1;
        break;
    default:
        return -1;
    }
    const u32 values_per_byte = 8 / bit_width;
    const u8 mask = static_cast<u8>((1U << bit_width) - 1U);
    for (std::size_t index = 0; index < count; ++index) {
        const u32 id = ids[index];
        if (id > maximum_id) {
            output[index] = 0;
            continue;
        }
        const auto byte = Read<u8>(memory, storage + bank_offset + id / values_per_byte);
        if (!byte) {
            return 0;
        }
        output[index] = static_cast<u8>((*byte >> ((id % values_per_byte) * bit_width)) & mask);
    }
    return static_cast<int>(count);
}

} // namespace XcxCompanion
