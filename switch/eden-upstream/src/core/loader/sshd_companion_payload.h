// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>

#include "common/common_types.h"

namespace Loader::SshdCompanionPayload {

constexpr u64 TitleId = 0x01002DA013484000ULL;
constexpr std::array<u8, 0x20> BuildId{
    0x54, 0x41, 0xAF, 0x3C, 0x89, 0xF9, 0x7E, 0x1D,
    0x25, 0xA8, 0xCC, 0xF1, 0x4C, 0x74, 0x2B, 0xF8,
    0x23, 0x36, 0xD8, 0xA8, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

constexpr u32 Magic = 0x4C505353; // "SSPL" in little-endian memory.
constexpr u32 Version = 1;

// Written by the injected, game-thread main-loop payload and read by the Android companion.
// Sequence is a seqlock: odd while the payload writes and even once a frame is complete.
struct Mailbox {
    u32 magic;
    u32 version;
    u32 size;
    u32 sequence;
    u64 file_manager;
    u64 save;
    u64 player;
    u64 story_flag_manager;
    u64 item_flag_manager;
    std::array<char, 8> stage_name;
    u8 current_room;
    u8 current_layer;
    u8 current_entrance;
    u8 current_night;
    u8 current_trial;
    std::array<u8, 3> reserved;
};

static_assert(sizeof(Mailbox) == 0x48);
static_assert(offsetof(Mailbox, sequence) == 0x0C);
static_assert(offsetof(Mailbox, file_manager) == 0x10);
static_assert(offsetof(Mailbox, stage_name) == 0x38);

} // namespace Loader::SshdCompanionPayload
