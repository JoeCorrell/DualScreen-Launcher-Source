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
constexpr u32 Version = 4;

enum class MapCaptureState : u32 {
    Idle = 0,
    Pending = 1,
    Ready = 2,
    Rejected = 3,
};

enum class MapCaptureResult : u32 {
    Success = 0,
    NoStage = 1,
    StageChanged = 2,
    NoTexture = 3,
    InvalidTexture = 4,
    NoMapGlobal = 5,
    InvalidMapScale = 6,
    InvalidCaptureWidth = 7,
    InvalidCaptureHeight = 8,
    InvalidMapFloor = 9,
    MissingMapFloor = 10,
    TextureWidthZero = 11,
    TextureHeightZero = 12,
    TextureDataMissing = 13,
};

// Written by the injected game-thread stage-render payload and read by the Android companion.
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

    // Host writes only map_request/map_acknowledged. The injected game-thread
    // bridge owns every other map field and is the only writer to dStage.
    // map_metadata_sequence is a seqlock: odd while metadata changes, even
    // after stage/texture/data/dimensions describe one completed capture.
    u32 map_metadata_sequence;
    u32 map_state;
    u32 map_request;
    u32 map_in_flight;
    u32 map_completed;
    u32 map_acknowledged;
    u32 map_result;
    u32 map_reserved;
    u64 map_stage;
    u64 map_texture;
    u64 map_data;
    u16 map_width;
    u16 map_height;
    u8 map_format;
    s8 map_floor;
    u16 map_reserved2;
    std::array<char, 8> map_stage_name;
    u32 ui_request;
    u32 ui_frames;
    u32 ui_status;
    u32 ui_guard;
    u64 ui_owner;
    u64 ui_texture;
    u64 ui_stage;
    u32 ui_last_request;
    u32 ui_idle_frames;
    u32 ui_frame_tick;
    u32 ui_drawn_tick;
    u32 ui_was_visible;
    u32 ui_reserved;
    u32 ui_gesture_sequence;
    float ui_gesture_zoom;
    float ui_gesture_pan_x;
    float ui_gesture_pan_y;
    u32 ui_gesture_applied;
    float ui_base_scroll_x;
    float ui_base_scroll_z;
    float ui_base_scale;
    std::array<u64, 3> ui_hint_panes;
};

static_assert(sizeof(Mailbox) == 0x108);
static_assert(offsetof(Mailbox, ui_gesture_sequence) == 0xD0);
static_assert(offsetof(Mailbox, ui_request) == 0x90);
static_assert(offsetof(Mailbox, sequence) == 0x0C);
static_assert(offsetof(Mailbox, file_manager) == 0x10);
static_assert(offsetof(Mailbox, stage_name) == 0x38);
static_assert(offsetof(Mailbox, map_metadata_sequence) == 0x48);
static_assert(offsetof(Mailbox, map_state) == 0x4C);
static_assert(offsetof(Mailbox, map_request) == 0x50);
static_assert(offsetof(Mailbox, map_stage) == 0x68);
static_assert(offsetof(Mailbox, map_data) == 0x78);
static_assert(offsetof(Mailbox, map_stage_name) == 0x88);

} // namespace Loader::SshdCompanionPayload
