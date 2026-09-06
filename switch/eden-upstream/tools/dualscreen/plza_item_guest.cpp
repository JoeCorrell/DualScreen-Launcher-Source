// SPDX-License-Identifier: GPL-3.0-or-later
// v393216 only. Called by the FIELD ball selector's own update, not a bag widget.
#include "../../src/core/loader/plza_item_protocol.h"
using namespace PlzaItem;
using u64 = uint64_t;
using u32 = uint32_t;
// Game reference return uses the AAPCS64 indirect-result register x8, not x0.
// A non-trivial destructor is required even though lifetime is released explicitly
// through the game's own type-specific routine below.
struct NativeRef { u64 object; ~NativeRef() {} };
#ifndef PLZA_ITEM_HOST_TEST
template<class T> static T& at(u64 p, unsigned offset) {
    return *reinterpret_cast<T*>(p + offset);
}
template<class R, class... A> static R call(u64 p, A... a) {
    return reinterpret_cast<R(*)(A...)>(p)(a...);
}
#endif
static bool pointer(u64 p) { return p >= 0x10000 && p < (1ULL << 39) && !(p & 7); }
static u64 token(u64 object) {
    if (!pointer(object)) return 0;
    const u64 control = at<u64>(object, 0x38);
    if (!pointer(control) || at<u64>(control, 0x10) != object + 0x28 ||
        !at<uint16_t>(control, 0x1a) || at<uint16_t>(control, 0x1a) == 0xffff ||
        at<u32>(control, 0x1c) == 0xffffffff) return 0;
    return at<u64>(control, 0);
}
static void finish(Mailbox& m, Result result) {
    m.result = result;
    __atomic_store_n(&m.state, result == Success ? Succeeded : Rejected, __ATOMIC_RELEASE);
    __atomic_store_n(&m.completed, m.request, __ATOMIC_RELEASE);
}
static u32 selected(u64 model) {
    const u64 begin = at<u64>(model, 0x50), end = at<u64>(model, 0x58);
    if (!pointer(begin) || end < begin || (end - begin) % 4 || end - begin > 3000 * 4) return 0;
    const u32 index = at<u32>(model, 0x4c);
    return index < (end - begin) / 4 ? at<u32>(begin, index * 4) : 0;
}
static void sampleItemKinds(u64 base, uint8_t* kinds, bool reset) {
    if (reset) for (u32 i = 0; i < ItemKindsCount; ++i) kinds[i] = UnknownKind;
    if (!at<u32>(base, 0x610dec0) || !at<u32>(base, 0x610a980)) return;
    const u64 db = at<u64>(base, 0x3ec3320);
    if (!pointer(db) || !at<u32>(db, 0x80)) return;
    auto bag = call<NativeRef>(base + 0x2bcac8);
    if (pointer(bag.object)) {
        u32 budget = 24; // bounded, cached static metadata, never queried per render frame
        for (u32 i = 1; i < ItemKindsCount && budget; ++i) {
            if (kinds[i] || at<int32_t>(bag.object, 0x44 + i * 16) <= 0) continue;
            --budget;
            // Same dispatch precedence as C150E4, from the game's item database.
            kinds[i] = call<bool>(base + 0x941600, db, i) ? RecoveryKind :
                call<bool>(base + 0x86de84, db, i) ? ExpKind :
                call<bool>(base + 0x40919c, db, i) ? MoveKind :
                call<bool>(base + 0x2acd9d4, db, i) ? EvolutionKind :
                call<bool>(base + 0x2acda1c, db, i) ? FormKind :
                call<bool>(base + 0x2acd858, db, i) ? NatureKind :
                call<bool>(base + 0x2acda64, db, i) ? EffortKind : OtherKind;
        }
    }
    call<void>(base + 0x99e6b4, &bag);
}
static void sampleParty(u64 base, PartyChannel& p, u64 root, u64 root_token) {
    const u32 request = __atomic_load_n(&p.request, __ATOMIC_ACQUIRE);
    if (request == __atomic_load_n(&p.completed, __ATOMIC_RELAXED)) return;
    const bool reset = p.root != root || p.root_token != root_token;
    p.count = 0; p.valid = 0; p.root = root; p.root_token = root_token;
    for (auto& member : p.members) member = {};
    if (root_token) {
        auto context = call<NativeRef>(base + 0x2bb680);
        if (context.object) {
            const u32 count = call<u32>(base + 0x43b10, context.object);
            if (count <= 6) {
                p.count = count; p.valid = 1;
                for (u32 i = 0; i < count; ++i) {
                    auto pokemon = call<NativeRef>(base + 0x4de870, context.object, i);
                    const u64 object = pokemon.object;
                    auto& member = p.members[i];
                    if (pointer(object) && at<u64>(object, 0) == base + 0x3e2a438 &&
                        pointer(at<u64>(object, 0x50)) &&
                        pointer(at<u64>(at<u64>(object, 0x50), 0x70)) && token(object)) {
                        member.identity = object; member.token = token(object);
                        // Exact native Pokemon accessors. They own PKM locking,
                        // decryption and checksum handling, on the game's thread.
                        member.species = call<uint16_t>(base + 0x3668, object);
                        member.form = call<uint16_t>(base + 0x3678, object);
                        member.level = call<uint8_t>(base + 0x975a0, object);
                        member.hp = call<uint16_t>(base + 0xdb9c, object);
                        member.max_hp = call<uint16_t>(base + 0x18d80, object);
                        member.status = call<uint16_t>(base + 0x5d30, object);
                        member.valid = member.species && member.species <= 1200 &&
                            member.level && member.level <= 100 && member.max_hp &&
                            member.hp <= member.max_hp;
                    }
                    if (!member.valid) p.valid = 0;
                    call<void>(base + 0x556ca0, &pokemon);
                }
            }
        }
        call<void>(base + 0x407900, &context);
        sampleItemKinds(base, reinterpret_cast<uint8_t*>(reinterpret_cast<u64>(&p) - PartyOffset + ItemKindsOffset), reset);
    }
    __atomic_store_n(&p.completed, request, __ATOMIC_RELEASE);
}
#include "plza_medicine_guest.h"
extern "C" __attribute__((section(".text.entry"), used))
void plza_item_guest(u64 base, Mailbox* mailbox, u64 presenter) {
    auto& m = *mailbox;
    if (m.magic != Magic || m.version != Version) return;
    const u64 root = at<u32>(base, 0x610b8c0) ? at<u64>(base, 0x610b810) : 0;
    const u64 root_token = token(root);
    const u64 model = at<u64>(presenter, 0x130); // alive for this native call
    const u32 current = pointer(model) ? selected(model) : 0;
    m.selected_ball = current;
    m.root = root;
    m.root_token = root_token;
    __atomic_add_fetch(&m.tick, 1U, __ATOMIC_RELEASE);
    sampleParty(base, *reinterpret_cast<PartyChannel*>(reinterpret_cast<u64>(mailbox) + PartyOffset), root, root_token);
    const u32 state = __atomic_load_n(&m.state, __ATOMIC_ACQUIRE);
    if (state != Queued && state != Running) return;
    if (!root_token || root != m.expected_root || root_token != m.expected_token) {
        finish(m, state == Running && m.operation == 2 ? EffectUnconfirmed : StaleSession); return;
    }
    if (!pointer(model) || !at<u32>(base, 0x610dc00) ||
        !at<u32>(base, 0x610d940) || !at<u32>(base, 0x610a980)) {
        finish(m, state == Running && m.operation == 2 ? EffectUnconfirmed : Unavailable); return;
    }
    if (state == Running) {
        if (m.operation == 2) {
            confirmMedicine(base, m, *reinterpret_cast<MedicineChannel*>(
                reinterpret_cast<u64>(mailbox) + MedicineOffset));
            return;
        }
        // The previous invocation returned through the game's original update:
        // 2BB680 -> 2BB90C -> 2BB7DC. Confirm both model AND persisted selection.
        // Don't dereference cached objects or fight subsequent controller changes.
        const u64 persistent = at<u64>(base, 0x610db60);
        const bool matched = model == m.model && current == m.item && token(persistent) &&
            at<u32>(persistent, 0x40) == m.item;
        finish(m, matched ? Success : SelectionChanged);
        return;
    }
    u32 expected = Queued;
    if (!__atomic_compare_exchange_n(&m.state, &expected, Running, false,
                                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return;
    if (!m.item || m.item >= 3000 ||
        !((m.operation == 1 && m.target == -1 && m.pouch == 1) ||
          (m.operation == 2 && m.target >= 0 && m.target < 6 &&
           (m.pouch == 0 || m.pouch == 2 || m.pouch == 5)))) {
        finish(m, Invalid); return;
    }
    // +110 suspends this presenter; +138 is the native mode; +13c contains
    // controller input awaiting this update. Never overwrite a competing input.
    const u32 mode = at<u32>(presenter, 0x138);
    if (at<uint8_t>(presenter, 0x110) || mode < 1 || mode > 2 ||
        at<uint8_t>(presenter, 0x13c)) {
        finish(m, Restricted); return;
    }
    if (m.operation == 2) {
        useMedicine(base, m, *reinterpret_cast<MedicineChannel*>(
            reinterpret_cast<u64>(mailbox) + MedicineOffset), model);
        return;
    }
    auto bag_ref = call<NativeRef>(base + 0x2bcac8);
    const u64 bag = bag_ref.object;
    const bool same_bag = bag == m.expected_bag && pointer(bag);
    const bool owned = same_bag && at<u32>(bag, 0x40 + m.item * 16) == 1 &&
        at<int32_t>(bag, 0x44 + m.item * 16) > 0;
    call<void>(base + 0x99e6b4, &bag_ref); // same release used by A5A268
    if (!same_bag) { finish(m, StaleSession); return; }
    if (!owned) { finish(m, ItemMissing); return; }
    // The native refresh rebuilds its game-owned vector, restores sorting and
    // persisted selection. No synthetic list, key presses or Satchel context.
    call<void>(base + 0xa88620, model);
    const u64 begin = at<u64>(model, 0x50), end = at<u64>(model, 0x58);
    if (!pointer(begin) || end < begin || (end - begin) % 4 || end - begin > 3000 * 4) {
        finish(m, Unavailable); return;
    }
    for (u32 i = 0; i < (end - begin) / 4; ++i) {
        if (at<u32>(begin, i * 4) != m.item) continue;
        const u64 persistent = at<u64>(base, 0x610db60);
        if (!token(persistent)) { finish(m, Unavailable); return; }
        // Exactly the model transition used by 9B6EB4/A87AD0, selecting the
        // requested index directly. Native 2BB7DC owns persistence/ref handling.
        at<u32>(model, 0x4c) = i;
        call<void>(base + 0x2bb7dc, model);
        at<uint8_t>(model, 0x74) = 1;
        m.index = i;
        m.model = model;
        return; // original native update applies SelectBall in this same tick
    }
    finish(m, ItemMissing);
}
