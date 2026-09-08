// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>

// C-layout, append-only ABI. No STL objects, JNI references or Eden class layouts
// cross this boundary. A module may change game offsets/UI/snapshot ABI itself.
#define DS_COMPANION_ABI 1u
struct DsSessionInfo {
    uint32_t size, running;
    uint64_t title_id, process_id, main_base, main_size, mailbox;
    uint8_t build_id[32];
};
struct DsNsoInfo {
    uint8_t build_id[32];
    uint32_t text_address, text_size, ro_address, ro_size, data_address, data_size, bss_size;
};
struct DsHostApi {
    uint32_t size, abi;
    void* context;
    uint32_t (*session)(void*, DsSessionInfo*);
    uint32_t (*read)(void*, uint64_t, void*, size_t);
    uint32_t (*write)(void*, uint64_t, const void*, size_t);
    uint32_t (*valid)(void*, uint64_t, size_t);
    void* (*map_mailbox)(void*, uint64_t, size_t);
    void (*log)(uint32_t, const char*);
    uint64_t (*romfs_size)(const char*);
    size_t (*romfs_read)(const char*, uint64_t, void*, size_t);
    uint8_t* (*decode_texture)(const uint8_t*, size_t, const char*, uint32_t, size_t*);
    void (*free_buffer)(void*);
    uint8_t* (*decode_rgba)(const uint8_t*, size_t, const char*, uint32_t, uint32_t*, uint32_t*, size_t*);
    uint8_t* (*encode_png)(const uint8_t*, size_t, uint32_t, uint32_t, size_t*);
};
struct DsModuleApi {
    uint32_t size, abi;
    uint64_t title_id;
    const char* version;
    uint32_t (*probe)(const DsNsoInfo*, const char*, uint32_t*, uint32_t*);
    uint32_t (*install)(uint8_t*, size_t, size_t, uint32_t, uint32_t, uint32_t);
    int32_t (*snapshot)(void*, size_t);
    int32_t (*action)(int32_t, int32_t, int32_t);
    size_t (*asset)(const char*, uint8_t*, size_t);
    size_t (*query)(int32_t, const char*, uint8_t*, size_t);
    int32_t (*action64)(int32_t, int64_t);
};
using DsModuleEntry = const DsModuleApi* (*)(const DsHostApi*);
