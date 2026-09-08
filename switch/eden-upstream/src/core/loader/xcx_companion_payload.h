// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>

#include "common/common_types.h"

namespace XcxCompanionPayload {

constexpr u64 TitleId = 0x0100453019AA8000ULL;
constexpr std::array<u8, 0x20> BuildId{
    0x2A, 0x72, 0x0C, 0x7C, 0xE5, 0xC8, 0x49, 0x05,
    0xC1, 0xC6, 0x84, 0x72, 0x11, 0xF6, 0x70, 0xFB,
    0xB3, 0x18, 0x42, 0xB6, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

constexpr u32 Magic = 0x42435858; // "XXCB" in little-endian guest memory.
constexpr u16 Version = 1;

enum class Action : u32 {
    None = 0,
    SwitchCharacter = 1,
    FastTravel = 2,
    InstallProbe = 3, // argument: site ID in low 16 bits, ITM_BeaconList ID in high 16.
    TrackMission = 4, // FLD_questlist row ID; zero clears mission navigation.
    SwapPartyMembers = 5, // Bytes: source slot, destination slot, expected source/destination IDs.
    EquipItem = 6, // argument: full inventory key; reserved: character | (UI slot << 8).
    EvaluateConditions = 7, // Internal read-only batch. argument=count; reserved=ignored condition types.
};

enum class State : u32 {
    Idle = 0,
    Queued = 1,
    Running = 2,
    Succeeded = 3,
    Rejected = 4,
};

enum class Result : u32 {
    None = 0,
    Success = 1,
    InvalidAction = 2,
    InvalidArgument = 3,
    EventScene = 4,
    PausedOrMenu = 5,
    MissingNativeContext = 6,
    NativeRejected = 7,
    Combat = 8,
    InSkell = 9,
};

struct Mailbox {
    u32 magic;
    u16 version;
    u16 size;
    u64 request;
    u64 completed;
    u32 state;
    u32 action;
    u32 argument;
    u32 result;
    u32 native_result;
    u32 reserved;
};

static_assert(sizeof(Mailbox) == 48);
static_assert(offsetof(Mailbox, request) == 8);
static_assert(offsetof(Mailbox, state) == 24);

constexpr u32 ConditionIdsOffset = 0x80;
constexpr u32 ConditionResultsOffset = 0x280;
constexpr u32 MaximumConditions = 128;

} // namespace XcxCompanionPayload
