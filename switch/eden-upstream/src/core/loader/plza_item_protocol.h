// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace PlzaItem {
constexpr uint32_t Magic = 0x5A414954; // ZAIT, separate from snapshot ABI
constexpr uint32_t Version = 1;
enum State : uint32_t { Idle, Queued, Running, Succeeded, Rejected };
enum Result : uint32_t {
    Success, Invalid = 4, Unavailable = 12, Unsupported = 15,
    StaleSession = 16, Restricted = 17, ItemMissing = 18,
    SelectionChanged = 19, Expired = 20, NoEffect = 21,
    TargetChanged = 22, EffectUnconfirmed = 23,
};
// First 0x80 bytes are the action channel; observers begin at 0x80.
// Host publishes fields before Queued, guest publishes completion last.
struct Mailbox {
    uint64_t request;
    uint64_t completed;
    uint32_t state;
    uint32_t operation;
    uint32_t item;
    int32_t target;
    uint32_t pouch;
    uint32_t index;
    uint32_t result;
    uint32_t magic;
    uint64_t expected_root;
    uint64_t expected_token;
    uint64_t expected_bag;
    uint64_t model; // identity only; never dereferenced on a later tick
    uint32_t selected_ball;
    uint32_t tick;
    uint32_t version;
    uint32_t reserved;
    uint64_t root_token;
    uint64_t root;
    uint64_t reserved2[2];
};
static_assert(sizeof(Mailbox) == 0x80);
static_assert(offsetof(Mailbox, state) == 0x10);
static_assert(offsetof(Mailbox, expected_root) == 0x30);
static_assert(offsetof(Mailbox, selected_ball) == 0x50);
constexpr uint32_t PartyOffset = 0x300;
struct PartyMember {
    uint64_t identity;
    uint64_t token;
    uint16_t species, form, level, hp, max_hp, status;
    uint32_t valid;
};
struct PartyChannel {
    uint32_t request, completed;
    uint32_t count, valid;
    uint64_t root, root_token;
    PartyMember members[6];
};
static_assert(sizeof(PartyMember) == 32);
static_assert(sizeof(PartyChannel) == 224);
constexpr uint32_t MedicineOffset = 0x400;
// Published before Queued. Only identities, never retained pointers across ticks.
// The guest resolves/retains the current party and native actor for each action.
struct MedicineChannel {
    uint64_t expected_pokemon, expected_token;
    uint32_t stage, consumed, start_tick, quantity_before, quantity_after;
    uint16_t hp_before, hp_after, status_before, status_after;
    uint32_t condition_before, condition_after;
};
static_assert(sizeof(MedicineChannel) == 56);
constexpr uint32_t ItemKindsOffset = 0x440;
constexpr uint32_t ItemKindsCount = 3000;
enum ItemKind : uint8_t { UnknownKind, RecoveryKind, ExpKind, MoveKind, EvolutionKind,
    FormKind, NatureKind, EffortKind, OtherKind };
static_assert(ItemKindsOffset + ItemKindsCount <= 0x1000);
}
