// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace SmoOutfit {
constexpr uint64_t TitleId = 0x0100000000010000ULL;
constexpr uint8_t BuildId[32] = {0x3c,0xa1,0x2d,0xfa,0xaf,0x9c,0x82,0xda,
    0x06,0x4d,0x16,0x98,0xdf,0x79,0xcd,0xa1};
constexpr uint32_t Magic = 0x574f4d53; // SMOW
constexpr uint32_t Version = 2;
enum State : uint32_t { Idle, Writing, Pending, Processing, Completed, Cancelled,
    Reloading, SpawnProvided };
enum Result : uint32_t {
    Success, NotOwned, WrongScene, StaleIdentity, PlayerBusy, InvalidPose,
    AlreadyEquipped, InvalidRequest, RestoreMismatch
};
constexpr char ReloadStartId[] = "DualScreenOutfitReload";
// Host owns request fields only while Writing. Release-publish Pending; the
// guest acquires/claims it once. Only the guest calls the game's functions.
struct Mailbox {
    uint32_t magic, version, size, state;
    uint32_t ticket, kind, result, reserved;
    uint64_t scene, holder, save, player;
    char name[64], expected_outfit[64], expected_hat[64];
    // Guest-owned one-shot reload context. Never serialized into game saves.
    char stage[64];
    float position[3], rotation[4];
    int32_t world, scenario;
    uint32_t spawn_count;
    // Native PlacementInfo (two ByamlIters) and PlayerStartInfo, constructed by
    // the game's constructors. Inline SafeString storage must not be relocated.
    alignas(8) uint8_t placement[0x20], start_info[0xd0];
    float observed_position[3];
    uint32_t reserved2;
};
static_assert(sizeof(Mailbox) == 616);
static_assert(offsetof(Mailbox, state) == 12 && offsetof(Mailbox, name) == 64);

inline bool Identifier(const char* value) {
    if (!value) return false;
    for (unsigned i = 0; i < 64; ++i) {
        const char c = value[i];
        if (!c) return i != 0;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return false;
}
inline bool Equal(const char* a, const char* b) {
    if (!a || !b) return false;
    for (unsigned i = 0; i < 64; ++i) {
        if (a[i] != b[i]) return false;
        if (!a[i]) return true;
    }
    return false;
}
inline bool Busy(uint32_t state) {
    return state == Writing || state == Pending || state == Processing ||
           state == Reloading || state == SpawnProvided;
}
} // namespace SmoOutfit
