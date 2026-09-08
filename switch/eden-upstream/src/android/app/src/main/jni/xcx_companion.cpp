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
#include <unordered_map>

#include "common/common_types.h"
#include "common/logging.h"
#include "core/core.h"
#include "core/hle/kernel/k_process.h"
#include "core/loader/xcx_companion_payload.h"
#include "core/loader/xcx_actor_vitals.h"
#include "core/memory.h"
#include "jni/native.h"

namespace XcxCompanion {
namespace {

constexpr u64 TitleId = XcxCompanionPayload::TitleId;
constexpr auto BuildId = XcxCompanionPayload::BuildId;

constexpr u32 Magic = 0x53584358; // "XCXS" in a little-endian direct buffer.
constexpr u16 Version = 9;
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
    CreditsState = 1U << 8,
    NavigationState = 1U << 9,
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
    u32 party_vitals_mask;
    u32 party_hp[PartyMemberCount];
    u32 party_maximum_hp[PartyMemberCount];
    u32 credits;
    u32 skell_fuel_mask;
    float skell_fuel[PartyMemberCount];
    float skell_maximum_fuel[PartyMemberCount];
    u32 party_level_mask;
    u32 party_levels[PartyMemberCount];
    u32 tracked_mission;
};
#pragma pack(pop)

static_assert(sizeof(SnapshotFrame) == 308);
static_assert(offsetof(SnapshotFrame, tracked_mission) == 304);
static_assert(offsetof(SnapshotFrame, party_level_mask) == 284);
static_assert(offsetof(SnapshotFrame, skell_fuel_mask) == 248);
static_assert(offsetof(SnapshotFrame, party_vitals_mask) == 208);
static_assert(offsetof(SnapshotFrame, credits) == 244);

std::atomic<u32> Sequence{0};
std::atomic<u64> LoggedRuntimeSignature{~0ULL};
std::atomic<u64> LoggedPlayerProbeSignature{~0ULL};
std::atomic<u64> TotalSampleTimeUs{0};
std::atomic<u64> MaximumSampleTimeUs{0};
std::atomic<u32> PerformanceSampleCount{0};
std::atomic<u64> ActionSequence{0};
std::atomic<u64> ReportedActionSequence{0};
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
    // 0x174D14: nonzero selects +0x0C, zero selects +0x10.
    const auto selection = Read<u8>(memory, slot + 0x16);
    if (!selection) return 0;
    return Read<u32>(memory, slot + (*selection != 0 ? 0x0C : 0x10)).value_or(0);
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
    if (!probe.handle_object) {
        return false;
    }
    // 0x3CA760 and 0x3D3EE0 calculate native distances from actor +0x50.
    // The animation root above is model-local and can remain at the origin.
    const auto x = Read<float>(memory, probe.handle_object + 0x50);
    const auto y = Read<float>(memory, probe.handle_object + 0x54);
    const auto z = Read<float>(memory, probe.handle_object + 0x58);
    const auto rotation_x = Read<float>(memory, probe.root_transform + 0x10);
    const auto rotation_y = Read<float>(memory, probe.root_transform + 0x14);
    const auto rotation_z = Read<float>(memory, probe.root_transform + 0x18);
    const auto rotation_w = Read<float>(memory, probe.root_transform + 0x1C);
    if (!x || !y || !z ||
        !std::isfinite(*x) || !std::isfinite(*y) || !std::isfinite(*z) ||
        std::abs(*x) > 10'000'000.0F || std::abs(*y) > 10'000'000.0F ||
        std::abs(*z) > 10'000'000.0F) {
        return false;
    }
    frame.player_x = *x;
    frame.player_y = *y;
    frame.player_z = *z;
    frame.player_transform_valid = 1; // Position survives missing animation/heading data.
    frame.capabilities |= PlayerTransform;
    if (!probe.root_transform || !rotation_x || !rotation_y || !rotation_z || !rotation_w ||
        !std::isfinite(*rotation_x) || !std::isfinite(*rotation_y) ||
        !std::isfinite(*rotation_z) || !std::isfinite(*rotation_w)) return true;
    const float rotation_length_squared = *rotation_x * *rotation_x + *rotation_y * *rotation_y +
                                          *rotation_z * *rotation_z + *rotation_w * *rotation_w;
    if (!std::isfinite(rotation_length_squared) || rotation_length_squared < 0.25F ||
        rotation_length_squared > 4.0F) {
        return true;
    }
    const float inverse_rotation_length = 1.0F / std::sqrt(rotation_length_squared);
    frame.player_x = *x;
    frame.player_y = *y;
    frame.player_z = *z;
    frame.player_rotation_x = *rotation_x * inverse_rotation_length;
    frame.player_rotation_y = *rotation_y * inverse_rotation_length;
    frame.player_rotation_z = *rotation_z * inverse_rotation_length;
    frame.player_rotation_w = *rotation_w * inverse_rotation_length;
    frame.player_transform_valid = 3; // Bit 1 additionally validates heading.
    frame.capabilities |= PlayerTransform;
    static std::atomic<u32> position_samples{};
    if (position_samples.fetch_add(1, std::memory_order_relaxed) % 120 == 0) {
        LOG_INFO(Frontend, "[XCX-POSITION] actor={:#x} world=({:.3f},{:.3f},{:.3f}) "
                 "model=({:.3f},{:.3f},{:.3f})", probe.handle_object, *x, *y, *z,
                 Read<float>(memory, probe.root_transform).value_or(0),
                 Read<float>(memory, probe.root_transform + 4).value_or(0),
                 Read<float>(memory, probe.root_transform + 8).value_or(0));
    }
    return true;
}

void ReadSkellState(Core::Memory::Memory& memory, VAddr main_base, SnapshotFrame& frame) {
    const auto slots = Read<u64>(memory, main_base + ControlledActorSlotsOffset).value_or(0);
    const auto pool = Read<u64>(memory, main_base + HandlePoolOffset).value_or(0);
    if (!slots || !pool) return;
    for (std::size_t index = 0; index < PartyMemberCount; ++index) {
        // 0x1751F8 returns the slot's Skell handle even while the pilot is on foot.
        const auto handle_address = slots + index * ControlledActorSlotStride + 0x10;
        const auto handle = Read<u32>(memory, handle_address).value_or(0);
        const auto object = ResolveHandleObject(memory, pool, handle).value_or(0);
        if (!object || Read<u8>(memory, object + 0xB4).value_or(0) != 4) continue;
        const auto component = Read<u64>(memory, object + 0x200).value_or(0);
        if (!component || Read<u64>(memory, component).value_or(0) != main_base + 0x1C313D8) continue;
        // Exact leaves 0x1CFA2C / 0x1CFA34, used by GetDollFuel / GetDollFuelRatio.
        const auto fuel = Read<float>(memory, component + 0x3CF0);
        const auto maximum = Read<float>(memory, component + 0x3CF4);
        if (!fuel || !maximum || !std::isfinite(*fuel) || !std::isfinite(*maximum) ||
            *fuel < 0 || *maximum <= 0 || *fuel > *maximum || *maximum > 1'000'000 ||
            Read<u32>(memory, handle_address).value_or(0) != handle ||
            Read<u64>(memory, object + 0x200).value_or(0) != component) continue;
        frame.skell_fuel_mask |= 1U << index;
        frame.skell_fuel[index] = *fuel;
        frame.skell_maximum_fuel[index] = *maximum;
    }
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
        if (character_id > 23) {
            continue;
        }

        if (character_id != 0) frame.party_valid_mask |= 1U << index;
        if (actor_slots && handle_pool) {
            const VAddr slot = actor_slots + index * ControlledActorSlotStride;
            // 0x174DCC stores the Inner handle at +0x0C. The selected handle
            // returned by 0x174D14 can instead be the Skell at +0x10.
            const u32 handle = Read<u32>(memory, slot + 0x0C).value_or(0);
            const VAddr object = ResolveHandleObject(memory, handle_pool, handle).value_or(0);
            if (object) {
                const auto object_identity = Read<u16>(memory, object + 0x28C);
                if (!object_identity) continue;
                const u16 object_character_id = *object_identity;
                const u16 actor_party_slot = Read<u16>(memory, object + 0x28E).value_or(0xFFFF);
                if (object_character_id == character_id) {
                    // Avatar ID zero is valid only when backed by the live slot's actor.
                    frame.party_valid_mask |= 1U << index;
                    frame.party_handles[index] = handle;
                    frame.party_actor_slots[index] = actor_party_slot;
                    const auto level = XcxCompanionPayload::ReadActorLevel(
                        [&](u64 address) { return Read<u8>(memory, address); },
                        [&](u64 address) { return Read<u64>(memory, address); }, main_base, object);
                    if (level && Read<u16>(memory, object + 0x28C) == character_id &&
                        Read<u32>(memory, slot + 0x0C) == handle) {
                        frame.party_levels[index] = *level;
                        frame.party_level_mask |= 1U << index;
                    }
                    const auto vitals = XcxCompanionPayload::ReadActorVitals(
                        [&](u64 address) { return Read<u32>(memory, address); },
                        [&](u64 address) { return Read<u64>(memory, address); },
                        main_base, object);
                    if (vitals && Read<u16>(memory, object + 0x28C) == character_id &&
                        Read<u32>(memory, slot + 0x0C) == handle) {
                        frame.party_hp[index] = vitals->hp;
                        frame.party_maximum_hp[index] = vitals->maximum_hp;
                        frame.party_vitals_mask |= 1U << index;
                    }
                }
                if (static_cast<s32>(index) == frame.active_party_slot) {
                    frame.player_character_id = object_character_id;
                }
            }
            if (static_cast<s32>(index) == frame.active_party_slot) {
                const auto controlled = ResolveHandleObject(
                    memory, handle_pool, ReadControlledActorHandle(memory, slot));
                if (controlled) {
                    frame.player_actor_type =
                        Read<u8>(memory, *controlled + 0xB4).value_or(0);
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
    ReportedActionSequence.store(frame.action_completed, std::memory_order_release);
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
    ReadSkellState(memory, main_base, frame);
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
        // addMoney -> 0x34A460 clamps and notifies after updating this native field.
        const auto credits = Read<u32>(memory, game_state + 0x1B5CC);
        if (credits && *credits <= 99'999'999) {
            frame.credits = *credits;
            frame.capabilities |= CreditsState;
        }
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
        // Mission confirmation: 0x56E58C -> 0x41B9EC, target field +0x54.
        const auto navigation = Read<u64>(memory, main_base + 0x1DF5978).value_or(0);
        const auto tracked = navigation ? Read<u32>(memory, navigation + 0x8150 + 0x54)
                                        : std::nullopt;
        if (tracked && *tracked <= 2558 &&
            Read<u64>(memory, main_base + 0x1DF5978).value_or(0) == navigation) {
            frame.tracked_mission = *tracked;
            frame.capabilities |= NavigationState;
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

bool PerformAction(EmulationSession& session, int action, int argument, unsigned int context) {
    using Payload = XcxCompanionPayload::Mailbox;
    using Action = XcxCompanionPayload::Action;
    using State = XcxCompanionPayload::State;
    const auto requested_action = static_cast<Action>(action);
    if ((requested_action != Action::SwitchCharacter && requested_action != Action::FastTravel &&
         requested_action != Action::InstallProbe && requested_action != Action::TrackMission &&
         requested_action != Action::SwapPartyMembers && requested_action != Action::EquipItem) ||
        (argument < 0 && requested_action != Action::EquipItem) ||
        (requested_action == Action::FastTravel && argument == 0)) {
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

    if (requested_action == Action::SwitchCharacter || requested_action == Action::SwapPartyMembers) {
        if (requested_action == Action::SwitchCharacter && argument > 23) {
            return false;
        }
        const VAddr game_state =
            Read<u64>(memory, main_base + GameStatePointerOffset).value_or(0);
        const VAddr actor_slots =
            Read<u64>(memory, main_base + ControlledActorSlotsOffset).value_or(0);
        const s32 active_slot = FindActivePartySlot(memory, actor_slots);
        const auto controlled = ProbePlayerObject(memory, main_base, active_slot);
        SnapshotFrame party{};
        for (auto& slot : party.party_actor_slots) slot = 0xFFFF;
        ReadPartyState(memory, main_base, game_state, party);
        bool member_present{};
        std::optional<u16> active_character;
        if (!game_state || !controlled.handle_object || active_slot < 0 ||
            Read<u8>(memory, controlled.handle_object + 0xB4).value_or(0) != 2) {
            return false;
        }
        for (std::size_t index = 0; index < PartyMemberCount; ++index) {
            const VAddr record = game_state + GameStatePartyOffset + index * PartyRecordStride;
            const u16 character_id = Read<u8>(memory, record + 1).value_or(0);
            const bool present = (party.party_valid_mask & (1U << index)) != 0;
            member_present |= present && character_id == static_cast<u16>(argument);
            if (present && static_cast<s32>(index) == active_slot) {
                active_character = character_id;
            }
        }
        if (requested_action == Action::SwitchCharacter) {
            if (!member_present || !active_character || *active_character == argument) return false;
        } else {
            const u32 from = argument & 0xFF;
            const u32 to = (argument >> 8) & 0xFF;
            if (from < 1 || from > 3 || to < 1 || to > 3 || from == to ||
                !(party.party_valid_mask & 1U) ||
                !(party.party_valid_mask & (1U << from)) ||
                !(party.party_valid_mask & (1U << to)) ||
                party.party_character_ids[from] != ((argument >> 16) & 0xFF) ||
                party.party_character_ids[to] != ((argument >> 24) & 0xFF)) return false;
        }
    } else if (requested_action == Action::InstallProbe) {
        const auto site = static_cast<u32>(argument) & 0xFFFF;
        const auto type = static_cast<u32>(argument) >> 16;
        if (site == 0 || site >= 144 || type == 0 || type > 39) return false;
        const auto manager = Read<u64>(memory, main_base + 0x1FEFCE0).value_or(0);
        const auto probes = manager ? Read<u64>(memory, manager + 0x10).value_or(0) : 0;
        const auto record = probes ? Read<u64>(memory, probes + site * 0x1D0).value_or(0) : 0;
        if (!record || Read<s8>(memory, record).value_or(0) <= 0) return false;
    } else if (requested_action == Action::TrackMission) {
        if (argument > 2558 ||
            !Read<u64>(memory, main_base + 0x1DF5978).value_or(0) ||
            !Read<u64>(memory, main_base + 0x1DFC440).value_or(0)) return false;
        // Active status is checked again by the native getter on the game thread.
    } else if (requested_action == Action::EquipItem) {
        const auto character = context & 255;
        const auto slot = (context >> 8) & 255;
        constexpr std::array<u32, 12> categories{6, 7, 1, 2, 4, 3, 5, 1, 2, 4, 3, 5};
        if (context >> 16 || character > 22 || slot >= categories.size() ||
            (static_cast<u32>(argument) & 63) != categories[slot]) return false;
        const auto root = Read<u64>(memory, main_base + GameStatePointerOffset).value_or(0);
        if (!root) return false;
        const auto record = root + 0xF8 + character * 0x7F8;
        const auto kind = Read<u8>(memory, record + 0x78).value_or(0);
        if ((kind != 1 && kind != 2) || Read<u8>(memory, record + 0x79) != character ||
            Read<u8>(memory, record + 0x7A).value_or(0) == 0) return false;
        // Ownership, slot compatibility and level are rechecked by the game-thread bridge.
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
        !StoreMailbox<u32>(system, mailbox, offsetof(Payload, reserved),
                           requested_action == Action::EquipItem ? context : 0U,
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

std::vector<unsigned char> ReadProbeInventory(EmulationSession& session) {
    [[maybe_unused]] auto system_access = session.LockSystemAccess();
    if (!session.IsRunning()) return {};
    auto& system = session.System();
    if (system.GetApplicationProcessProgramID() != TitleId ||
        system.GetApplicationProcessBuildID() != BuildId) return {};
    const auto [main_base, main_size] = system.GetApplicationProcessMainModule();
    if (!main_base || main_size < 0x1FEFCE8) return {};
    auto& memory = system.ApplicationMemory();
    // 0x321DA8 -> 0x13DC50(category 28). Category's vector index is 8;
    // 0x13C210 / 0x13B6C8 supply its start, end and byte stride.
    const auto inventory = main_base + 0x1F919A0;
    const auto start = Read<u64>(memory, inventory + 0x148);
    const auto end = Read<u64>(memory, inventory + 0x150);
    const auto stride = Read<u16>(memory, inventory + 0x16A);
    const auto manager = Read<u64>(memory, main_base + 0x1FEFCE0).value_or(0);
    const auto probes = manager ? Read<u64>(memory, manager + 0x10).value_or(0) : 0;
    if (!start || !end || !stride || !*start || *end < *start || *stride < 4 ||
        *stride > 64 || *stride % 4 || (*end - *start) % *stride || (*end - *start) / *stride > 4096 ||
        !probes || !memory.IsValidVirtualAddressRange(*start, *end - *start)) return {};
    std::array<u32, 40> owned{};
    for (auto address = *start; address < *end; address += *stride) {
        const auto packed = Read<u32>(memory, address);
        if (!packed) return {};
        const auto id = *packed & 0x1FFF;
        auto category = (*packed >> 13) & 63;
        if (category == 63) {
            if (*stride < 16) return {};
            category = Read<u32>(memory, address + 12).value_or(0);
        }
        if (category == 28 && id > 0 && id < owned.size()) owned[id] = (*packed >> 19) & 255;
    }
    // Pairs of owned and installed counts, IDs 1..39; basic probes have no inventory cost.
    std::vector<unsigned char> result(39 * 8);
    for (u32 id = 1; id <= 39; ++id) {
        const auto used = Read<u32>(memory, probes + 0x130D4 + id * 4);
        if (!used || *used > 143) return {};
        std::memcpy(result.data() + (id - 1) * 8, &owned[id], 4);
        std::memcpy(result.data() + (id - 1) * 8 + 4, &*used, 4);
    }
    if (Read<u64>(memory, inventory + 0x148) != start ||
        Read<u64>(memory, inventory + 0x150) != end ||
        Read<u16>(memory, inventory + 0x16A) != stride ||
        Read<u64>(memory, manager + 0x10).value_or(0) != probes) return {};
    return result;
}

std::vector<unsigned char> ReadConditions(EmulationSession& session,
    const std::vector<unsigned int>& ids, unsigned int ignored_types) {
    using Payload = XcxCompanionPayload::Mailbox;
    using State = XcxCompanionPayload::State;
    constexpr u32 Query = static_cast<u32>(XcxCompanionPayload::Action::EvaluateConditions);
    if ((ignored_types != 0 && ignored_types != 8) ||
        ids.empty() || ids.size() > XcxCompanionPayload::MaximumConditions ||
        std::any_of(ids.begin(), ids.end(), [](u32 id) { return id > 8974; })) return {};
    std::scoped_lock action_lock{ActionMutex};
    [[maybe_unused]] auto system_access = session.LockSystemAccess();
    if (!session.IsRunning()) return {};
    auto& system = session.System();
    if (system.GetApplicationProcessProgramID() != TitleId ||
        system.GetApplicationProcessBuildID() != BuildId) return {};
    const auto [main_base, main_size] = system.GetApplicationProcessMainModule();
    const auto mailbox = system.GetApplicationProcessCompanionMailbox();
    if (!main_base || main_size < GameStatePointerOffset + 8 ||
        !IsActionMailboxAvailable(system, mailbox) ||
        LoadMailbox<u32>(system, mailbox, offsetof(Payload, magic), __ATOMIC_ACQUIRE) != XcxCompanionPayload::Magic ||
        LoadMailbox<u16>(system, mailbox, offsetof(Payload, version), __ATOMIC_RELAXED) != XcxCompanionPayload::Version ||
        LoadMailbox<u16>(system, mailbox, offsetof(Payload, size), __ATOMIC_RELAXED) != sizeof(Payload)) return {};
    const auto root = Read<u64>(system.ApplicationMemory(), main_base + GameStatePointerOffset).value_or(0);
    if (!root) return {};
    struct Cached { u64 time; u8 value; };
    static std::unordered_map<u64, Cached> cache;
    static u64 cache_base{}, cache_root{}, cache_process{}, consumed{};
    const auto process_id = system.ApplicationProcess()->GetProcessId();
    if (cache_base != main_base || cache_root != root || cache_process != process_id) {
        cache.clear(); consumed = 0;
        cache_base = main_base; cache_root = root; cache_process = process_id;
    }
    const auto now = static_cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    const auto state = static_cast<State>(LoadMailbox<u32>(system, mailbox,
        offsetof(Payload, state), __ATOMIC_ACQUIRE));
    const auto action = LoadMailbox<u32>(system, mailbox, offsetof(Payload, action), __ATOMIC_RELAXED);
    const auto completed = LoadMailbox<u64>(system, mailbox, offsetof(Payload, completed), __ATOMIC_ACQUIRE);
    const auto pending = LoadMailbox<u64>(system, mailbox, offsetof(Payload, request), __ATOMIC_ACQUIRE);
    if (action == Query && state == State::Succeeded && !pending && completed != consumed) {
        const auto count = LoadMailbox<u32>(system, mailbox, offsetof(Payload, argument), __ATOMIC_RELAXED);
        const auto mask = LoadMailbox<u32>(system, mailbox, offsetof(Payload, reserved), __ATOMIC_RELAXED);
        const auto written = LoadMailbox<u32>(system, mailbox, offsetof(Payload, native_result), __ATOMIC_RELAXED);
        if (count <= XcxCompanionPayload::MaximumConditions && count == written) {
            for (u32 index = 0; index < count; ++index) {
                const auto id = LoadMailbox<u32>(system, mailbox,
                    XcxCompanionPayload::ConditionIdsOffset + index * 4, __ATOMIC_RELAXED);
                const auto value = LoadMailbox<u8>(system, mailbox,
                    XcxCompanionPayload::ConditionResultsOffset + index, __ATOMIC_RELAXED);
                if (id <= 8974 && value <= 1) cache[(static_cast<u64>(mask) << 32) | id] = {now, value};
            }
        }
        consumed = completed;
    }
    bool refresh{};
    bool complete{true};
    std::vector<unsigned char> result;
    result.reserve(ids.size());
    for (auto id : ids) {
        const auto found = cache.find((static_cast<u64>(ignored_types) << 32) | id);
        if (found == cache.end() || now - found->second.time > 2000) complete = false;
        if (found == cache.end() || now - found->second.time >= 400) refresh = true;
        result.push_back(found == cache.end() ? 0 : found->second.value);
    }
    // A background read must not overwrite a user's completion before a snapshot reports it.
    const bool reported = action == Query || action == 0 ||
        completed == ReportedActionSequence.load(std::memory_order_acquire);
    if (refresh && reported && !pending &&
        (state == State::Idle || state == State::Succeeded || state == State::Rejected)) {
        bool stored = true;
        for (size_t index = 0; index < ids.size(); ++index)
            stored &= StoreMailbox<u32>(system, mailbox,
                XcxCompanionPayload::ConditionIdsOffset + static_cast<u32>(index) * 4,
                ids[index], __ATOMIC_RELAXED);
        const auto sequence = std::max(ActionSequence.fetch_add(1, std::memory_order_relaxed) + 1, completed + 1);
        ActionSequence.store(sequence, std::memory_order_relaxed);
        if (stored && StoreMailbox<u32>(system, mailbox, offsetof(Payload, action), Query, __ATOMIC_RELAXED) &&
            StoreMailbox<u32>(system, mailbox, offsetof(Payload, argument), static_cast<u32>(ids.size()), __ATOMIC_RELAXED) &&
            StoreMailbox<u32>(system, mailbox, offsetof(Payload, reserved), ignored_types, __ATOMIC_RELAXED) &&
            StoreMailbox<u32>(system, mailbox, offsetof(Payload, result), 0, __ATOMIC_RELAXED) &&
            StoreMailbox<u32>(system, mailbox, offsetof(Payload, native_result), 0, __ATOMIC_RELAXED) &&
            StoreMailbox<u64>(system, mailbox, offsetof(Payload, request), sequence, __ATOMIC_RELAXED)) {
            StoreMailbox<u32>(system, mailbox, offsetof(Payload, state), static_cast<u32>(State::Queued), __ATOMIC_RELEASE);
        }
    }
    return complete ? result : std::vector<unsigned char>{};
}

std::vector<unsigned char> ReadEquipmentInventory(EmulationSession& session, unsigned int category) {
    if (category < 1 || category > 7) return {};
    [[maybe_unused]] auto system_access = session.LockSystemAccess();
    if (!session.IsRunning()) return {};
    auto& system = session.System();
    if (system.GetApplicationProcessProgramID() != TitleId ||
        system.GetApplicationProcessBuildID() != BuildId) return {};
    const auto [main_base, main_size] = system.GetApplicationProcessMainModule();
    if (!main_base || main_size < 0x1F91A70) return {};
    auto& memory = system.ApplicationMemory();
    // 0x13C210 and table 0x17EFAF8: armor shares vector 3; ranged/melee use 1/2.
    // Read the complete allocation and skip empty records, as native iterators do.
    const auto inventory = main_base + 0x1F919A0;
    const u32 index = category <= 5 ? 3 : category == 6 ? 1 : 2;
    const auto vector = inventory + index * 0x28;
    const auto start = Read<u64>(memory, vector + 8);
    const auto end = Read<u64>(memory, vector + 16);
    const auto stride = Read<u16>(memory, vector + 0x2A);
    if (!start || !end || !stride || !*start || *end < *start || *stride != 64 ||
        (*end - *start) % *stride || (*end - *start) / *stride > 4096) return {};
    const auto length = static_cast<size_t>(*end - *start);
    if (length && !memory.IsValidVirtualAddressRange(*start, length)) return {};
    std::vector<u8> records(length), verify(length);
    if (length && !memory.ReadBlockUnsafe(*start, records.data(), length)) return {};
    std::vector<u32> packet{0x49455858, 1, category, 0}; // XXEI
    for (size_t at = 0; at < length; at += *stride) {
        u32 packed{}, identity{}, extended{};
        std::memcpy(&packed, records.data() + at, 4);
        std::memcpy(&identity, records.data() + at + 8, 4);
        std::memcpy(&extended, records.data() + at + 12, 4);
        const u32 id = packed & 0x1FFF;
        const u32 stored_category = ((packed >> 13) & 63) == 63 ? extended : (packed >> 13) & 63;
        if (!id || stored_category != category) continue;
        // 0x13C490 reconstructs the full native key; never use the static ID as a key.
        const u32 key = (identity << 6) | category;
        packet.insert(packet.end(), {id, key, packed, identity});
        ++packet[3];
    }
    if ((length && (!memory.ReadBlockUnsafe(*start, verify.data(), length) || records != verify)) ||
        Read<u64>(memory, vector + 8) != start || Read<u64>(memory, vector + 16) != end ||
        Read<u16>(memory, vector + 0x2A) != stride) return {};
    std::vector<unsigned char> result(packet.size() * sizeof(u32));
    std::memcpy(result.data(), packet.data(), result.size());
    return result;
}

std::vector<unsigned char> ReadEquipment(EmulationSession& session, unsigned int character) {
    if (character > 22) return {};
    [[maybe_unused]] auto system_access = session.LockSystemAccess();
    if (!session.IsRunning()) return {};
    auto& system = session.System();
    if (system.GetApplicationProcessProgramID() != TitleId ||
        system.GetApplicationProcessBuildID() != BuildId) return {};
    const auto [main_base, main_size] = system.GetApplicationProcessMainModule();
    if (!main_base || main_size < GameStatePointerOffset + sizeof(u64)) return {};
    auto& memory = system.ApplicationMemory();
    const auto root = Read<u64>(memory, main_base + GameStatePointerOffset);
    if (!root || !*root) return {};
    // Native 0x34AC50/0x56BDB4 character-copy layout; 0x405B1C equipment offsets.
    const auto address = *root + 0xF8 + character * 0x7F8;
    std::array<u8, 0x348> record{}, verify{};
    if (!memory.IsValidVirtualAddressRange(address, record.size()) ||
        !memory.ReadBlockUnsafe(address, record.data(), record.size()) ||
        (record[0x78] != 1 && record[0x78] != 2) || record[0x79] != character ||
        record[0x7A] == 0) return {};
    constexpr std::array<u32, 12> offsets{
        0x80, 0xB4, 0x11C, 0xE8, 0x184, 0x150, 0x1B8, 0x220, 0x1EC, 0x288, 0x254, 0x2BC};
    // Exact 0x405D28 category table at main+0x17F6CBC.
    constexpr std::array<u32, 12> categories{6, 7, 1, 2, 4, 3, 5, 1, 2, 4, 3, 5};
    std::array<u32, 4 + 12 * 4> packet{0x51455858, 1, character, 12}; // XXEQ
    for (size_t slot = 0; slot < offsets.size(); ++slot) {
        u16 item{};
        u32 instance{};
        std::memcpy(&item, record.data() + offsets[slot], sizeof(item));
        std::memcpy(&instance, record.data() + offsets[slot] + 4, sizeof(instance));
        if (item > 8191) return {};
        packet[4 + slot * 4] = static_cast<u32>(slot);
        packet[5 + slot * 4] = categories[slot];
        packet[6 + slot * 4] = item;
        packet[7 + slot * 4] = item ? instance : 0;
    }
    if (!memory.ReadBlockUnsafe(address, verify.data(), verify.size()) || record != verify ||
        Read<u64>(memory, main_base + GameStatePointerOffset) != root) return {};
    std::vector<unsigned char> result(sizeof(packet));
    std::memcpy(result.data(), packet.data(), result.size());
    return result;
}

std::vector<unsigned char> ReadMapSegments(EmulationSession& session, unsigned int field) {
    if (field == 0 || field > 19) return {};
    [[maybe_unused]] auto system_access = session.LockSystemAccess();
    if (!session.IsRunning()) return {};
    auto& system = session.System();
    if (system.GetApplicationProcessProgramID() != TitleId ||
        system.GetApplicationProcessBuildID() != BuildId) return {};
    const auto [main_base, main_size] = system.GetApplicationProcessMainModule();
    constexpr VAddr root_offset = 0x26D8530;
    if (!main_base || main_size < root_offset + 8) return {};
    auto& memory = system.ApplicationMemory();
    const auto root = Read<u64>(memory, main_base + root_offset);
    if (!root || !*root) return {};
    const auto entry = *root + field * SegmentEntryStride;
    const auto count = Read<u64>(memory, entry + 0x1230);
    if (!count || *count > 128) return {};
    const auto size = static_cast<std::size_t>(*count) * 0x24;
    if (!memory.IsValidVirtualAddressRange(entry + 0x30, size)) return {};
    std::vector<unsigned char> result(size);
    if (size && !memory.ReadBlockUnsafe(entry + 0x30, result.data(), size)) return {};
    if (Read<u64>(memory, main_base + root_offset) != root ||
        Read<u64>(memory, entry + 0x1230) != count) return {};
    // 0x563B60 joins kind-2 cell IDs against these live 0x1C-byte content records.
    // Only source type 2 denotes a quest; other IDs are not quest IDs.
    const auto content_count = Read<u64>(memory, entry + 0x2430);
    if (!content_count || *content_count > 100) return {};
    const auto content_size = static_cast<std::size_t>(*content_count) * 0x1C;
    std::vector<unsigned char> contents(content_size);
    if (content_size && (!memory.IsValidVirtualAddressRange(entry + 0x1940, content_size) ||
        !memory.ReadBlockUnsafe(entry + 0x1940, contents.data(), content_size))) return {};
    if (Read<u64>(memory, entry + 0x2430) != content_count ||
        Read<u64>(memory, main_base + root_offset) != root) return {};
    // Host packet: original 36 bytes, installed probe, quest reference, visibility flags.
    // 0x324FFC -> 0x321BB4 reads a signed byte through
    // the probe array's 0x1D0-stride pointer slots. Never write guest records.
    const auto probe_manager = Read<u64>(memory, main_base + 0x1FEFCE0).value_or(0);
    const auto probes = probe_manager ? Read<u64>(memory, probe_manager + 0x10).value_or(0) : 0;
    // Native 0x5641B4 uses 0xC97A8's 16-bit story progression to hide early probe icons.
    const auto flag_manager = Read<u64>(memory, main_base + 0x1F94DF8).value_or(0);
    const auto flag_object = flag_manager ? Read<u64>(memory, flag_manager + 8).value_or(0) : 0;
    const auto flag_storage = flag_object ? Read<u64>(memory, flag_object).value_or(0) : 0;
    const auto progression = flag_storage ? Read<u16>(memory, flag_storage + 0x5802) : std::nullopt;
    std::vector<unsigned char> packet(static_cast<std::size_t>(*count) * 48);
    for (std::size_t index = 0; index < *count; ++index) {
        const auto* record = result.data() + index * 36;
        auto* output = packet.data() + index * 48;
        std::memcpy(output, record, 36);
        const u32 probe_id = record[16] | (static_cast<u32>(record[17]) << 8);
        const u32 state_flag = record[18] | (static_cast<u32>(record[19]) << 8);
        if (record[10] != 1 || state_flag == 1 || progression) {
            output[44] = 1; // Visibility predicate available.
            if (record[10] == 1 && state_flag != 1 && *progression < 2008) output[44] |= 2;
        }
        // 0x562DE0: related-tip marker. 0x54AD9C negates GameCondition
        // C7A542CD -> scenario row 32 [0,2007], so it opens at progression 2008.
        const u32 related_id = record[28] | (static_cast<u32>(record[29]) << 8);
        const u32 related_flag = record[30] | (static_cast<u32>(record[31]) << 8);
        const auto flag_set = [&](u32 id) {
            const auto value = flag_storage ? Read<u8>(memory, flag_storage + id / 8) : std::nullopt;
            return value && ((*value >> (id % 8)) & 1) != 0;
        };
        const u32 discovery_flag = record[8] | (static_cast<u32>(record[9]) << 8);
        if (progression && *progression >= 2008 && related_id != 0 &&
            (record[10] != 1 || flag_set(discovery_flag)) &&
            (related_flag == 0 || flag_set(related_flag))) output[44] |= 4;
        if (record[10] == 1 && probes && probe_id < 144) {
            const auto data = Read<u64>(memory, probes + probe_id * 0x1D0).value_or(0);
            const auto item = data ? Read<s8>(memory, data).value_or(0) : 0;
            output[36] = item > 0 && item <= 39 ? static_cast<u8>(item) : 0;
        }
        if (record[10] == 2) {
            const u32 id = record[12] | (static_cast<u32>(record[13]) << 8);
            for (std::size_t at = 0; at < content_size; at += 0x1C) {
                u32 content_id{};
                std::memcpy(&content_id, contents.data() + at + 12, 4);
                if (content_id != id) continue;
                if (contents[at + 0x15] == 2) {
                    const u32 quest = contents[at + 0x16] |
                                      (static_cast<u32>(contents[at + 0x17]) << 8);
                    if (quest <= 2558) std::memcpy(output + 40, &quest, 4);
                }
                break;
            }
        }
    }
    if (Read<u64>(memory, main_base + root_offset) != root ||
        Read<u64>(memory, entry + 0x1230) != count ||
        Read<u64>(memory, entry + 0x2430) != content_count ||
        Read<u64>(memory, main_base + 0x1F94DF8).value_or(0) != flag_manager ||
        (flag_manager && Read<u64>(memory, flag_manager + 8).value_or(0) != flag_object) ||
        (flag_object && Read<u64>(memory, flag_object).value_or(0) != flag_storage) ||
        (progression && Read<u16>(memory, flag_storage + 0x5802) != progression)) return {};
    return packet;
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
