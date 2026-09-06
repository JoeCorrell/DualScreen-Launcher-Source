// SPDX-License-Identifier: GPL-3.0-or-later
// v393216 Recovery task's gameplay-only path. No Satchel/task/widget fabrication.
// See docs/plza-item-actions.md for the native callers and AAPCS64 return ABI.
struct NativeDescriptor { u64 words[4]; ~NativeDescriptor() {} };
struct NativeBinding { u64 first, second, owner; ~NativeBinding() {} };
static void releaseVirtual(u64 object, unsigned offset) {
    if (object) call<void>(at<u64>(at<u64>(object, 0), offset), object);
}
static bool validPokemon(u64 base, u64 object) {
    return pointer(object) && at<u64>(object, 0) == base + 0x3e2a438 &&
        pointer(at<u64>(object, 0x50)) &&
        pointer(at<u64>(at<u64>(object, 0x50), 0x70)) && token(object);
}
// A5C51C's actual actor GUID -> live Pokemon resolution. Every returned reference
// has its own native release; NativeRef's destructor only selects the x8 ABI.
static NativeRef actorPokemon(u64 base, u64 actor, u64 service) {
    NativeRef result{0};
    auto guid = call<NativeRef>(base + 0x8eae80, actor); // value, not a refcounted object
    u64 descriptor[2]{at<u64>(base, 0x3ec2228) + 0x10, guid.object};
    auto lookup = call<NativeRef>(base + 0x5f8d2c, service, descriptor);
    if (!call<bool>(base + 0x5f8e90, &lookup)) {
        auto entry = call<NativeRef>(base + 0x9beab0, &lookup);
        if (entry.object) {
            auto source = call<NativeRef>(base + 0x5534e0, entry.object);
            call<void>(base + 0x52d688, &result, &source);
            call<void>(base + 0x556ca0, &source);
        }
        call<void>(base + 0xb63b0, &entry);
    }
    return result;
}
// The C31D64 eligibility branches, calling native metadata/condition predicates.
// A5C988, not this adapter, calculates and applies the actual recovery effect.
static bool medicineEligible(u64 base, u64 db, u32 item, NativeRef& pokemon,
                             u64 context, u32 slot, MedicineChannel& d, bool& healing) {
    const u64 object = pokemon.object;
    if (call<bool>(base + 0xc31f10, context, slot) ||
        call<bool>(base + 0xc31f84, context)) return false;
    const u32 fixed = call<u32>(base + 0xa5b588, db, item);
    const u32 percent = call<u32>(base + 0xa5b5c8, db, item);
    const u32 revive_percent = call<u32>(base + 0xa5b548, db, item);
    const u32 amount = (percent || revive_percent) ?
        call<u32>(base + 0xa5b608, &pokemon, percent > revive_percent ? percent : revive_percent) : fixed;
    healing = amount && d.hp_before < call<uint16_t>(base + 0x18d80, object);
    const bool status = call<bool>(base + 0x941b80, db, item);
    const bool condition = call<bool>(base + 0xa5cb18, db, item);
    d.condition_before = call<bool>(base + 0xc31ff0, object);
    if (status) {
        if (d.status_before) return call<bool>(base + 0xc32194, db, item, u32(d.status_before));
        if (d.condition_before) return condition;
        if (!healing) return false;
    }
    if (condition) {
        if (d.condition_before) return true;
        if (!healing) return false;
    }
    const bool revive = revive_percent || call<u32>(base + 0xa5b7e0, db, item);
    if (revive || !d.hp_before) return revive && !d.hp_before;
    return healing && !call<bool>(base + 0x18e44, object, 2U);
}
static void confirmMedicine(u64 base, Mailbox& m, MedicineChannel& d) {
    auto context = call<NativeRef>(base + 0x2bb680);
    NativeRef pokemon{0};
    if (context.object && u32(m.target) < call<u32>(base + 0x43b10, context.object))
        pokemon = call<NativeRef>(base + 0x4de870, context.object, u32(m.target));
    if (!validPokemon(base, pokemon.object) || pokemon.object != d.expected_pokemon ||
        token(pokemon.object) != d.expected_token) {
        finish(m, EffectUnconfirmed); // already consumed: never retry/refund a delayed effect
    } else {
        d.hp_after = call<uint16_t>(base + 0xdb9c, pokemon.object);
        d.status_after = call<uint16_t>(base + 0x5d30, pokemon.object);
        d.condition_after = call<bool>(base + 0xc31ff0, pokemon.object);
        auto bag = call<NativeRef>(base + 0x2bcac8);
        const bool same_bag = bag.object == m.expected_bag && pointer(bag.object);
        if (same_bag) d.quantity_after = at<u32>(bag.object, 0x44 + m.item * 16);
        call<void>(base + 0x99e6b4, &bag);
        const bool effect = d.hp_after > d.hp_before ||
            (d.status_before && !d.status_after) || (d.condition_before && !d.condition_after);
        if (same_bag && d.quantity_after + 1 == d.quantity_before && effect) {
            d.stage = 8; finish(m, Success);
        } else if (!same_bag || u32(m.tick - d.start_tick) >= 120) {
            finish(m, EffectUnconfirmed);
        }
    }
    call<void>(base + 0x556ca0, &pokemon);
    call<void>(base + 0x407900, &context);
}
static void useMedicine(u64 base, Mailbox& m, MedicineChannel& d, u64 model) {
    d.stage = 1; d.consumed = 0; d.start_tick = m.tick;
    const u64 db = at<u64>(base, 0x3ec3320);
    const u64 service = at<u64>(base, 0x3ec2c38);
    const u64 inventory = at<u64>(base, 0x3ec32b0);
    const u64 records = at<u64>(base, 0x3ec68d8);
    const u64 allocator = at<u64>(model, 8); // A89188 uses this exact allocator for native lists
    if (!at<u32>(base, 0x610dec0) || !pointer(db) || !at<u32>(db, 0x80) ||
        !pointer(service) || !at<u32>(service, 0x140) ||
        !pointer(inventory) || !at<u32>(inventory, 0x140) ||
        !pointer(records) || !at<u32>(records, 0x280) || !at<u32>(base, 0x610e100) ||
        !pointer(allocator) || !pointer(at<u64>(allocator, 0))) {
        finish(m, Unavailable); return;
    }
    // A5C670 uses allocator v+10 to construct the game's own MedicineAdapter.
    const u64 allocate = at<u64>(at<u64>(allocator, 0), 0x10);
    if (allocate < base || allocate >= base + 0x3163f70) { finish(m, Unavailable); return; }
    if (!call<bool>(base + 0x941600, db, m.item)) { finish(m, Unsupported); return; }
    auto context = call<NativeRef>(base + 0x2bb680);
    NativeRef pokemon{0}, wrapper{0}, actor{0}, mapped{0}, adapter{0};
    NativeRef status_service{0}, status_target{0};
    NativeBinding binding{};
    bool healing = false;
    Result failure = TargetChanged;
    do {
        if (!context.object || u32(m.target) >= call<u32>(base + 0x43b10, context.object)) break;
        pokemon = call<NativeRef>(base + 0x4de870, context.object, u32(m.target));
        if (!validPokemon(base, pokemon.object) || pokemon.object != d.expected_pokemon ||
            token(pokemon.object) != d.expected_token) break;
        d.stage = 2;
        d.hp_before = call<uint16_t>(base + 0xdb9c, pokemon.object);
        d.status_before = call<uint16_t>(base + 0x5d30, pokemon.object);
        failure = NoEffect;
        if (!medicineEligible(base, db, m.item, pokemon, context.object, u32(m.target), d, healing)) break;
        failure = Unavailable;
        wrapper = call<NativeRef>(base + 0xa5bd0c, context.object, u32(m.target));
        if (!wrapper.object) break;
        actor = call<NativeRef>(base + 0x2d2c78, wrapper.object);
        if (!pointer(actor.object) || at<u64>(actor.object, 0) != base + 0x3be0d28) break;
        mapped = actorPokemon(base, actor.object, service);
        failure = TargetChanged;
        if (mapped.object != pokemon.object) break;
        if (call<bool>(base + 0x941b80, db, m.item) || call<bool>(base + 0xa5cb18, db, m.item)) {
            failure = Unavailable;
            // CA9F2C/CAA010 use this additional native status-target registry.
            status_service = call<NativeRef>(base + 0x52d5dc, base + 0x610b780);
            if (!status_service.object) break;
            auto guid = call<NativeRef>(base + 0x8eae80, actor.object);
            u64 key[2]{at<u64>(base, 0x3ec2228) + 0x10, guid.object};
            status_target = call<NativeRef>(base + 0xd88b8, status_service.object, key);
            if (!status_target.object) break;
        }
        d.stage = 3;
        NativeBinding scratch{};
        auto descriptor = call<NativeDescriptor>(base + 0xdb220, actor.object + 0x118, &scratch);
        binding = call<NativeBinding>(base + 0x72af0, &descriptor);
        failure = Restricted;
        if (!pointer(binding.first) || !pointer(binding.second) || !pointer(binding.owner) ||
            at<uint8_t>(binding.owner, 0x130 + 0xa1)) break;
        adapter = call<NativeRef>(base + 0xa5c670, allocator, &actor);
        failure = Unavailable;
        if (!pointer(adapter.object) || at<u64>(adapter.object, 0) != base + 0x3da1970 ||
            at<u64>(adapter.object, 0x30) != actor.object) break;
        d.stage = 4;
        // All native target/effect dependencies are retained before consumption.
        // Same order as A598A4: native bag mutation, usage records, native effect.
        auto bag = call<NativeRef>(base + 0x2bcac8);
        const bool owned = bag.object == m.expected_bag && pointer(bag.object) &&
            at<u32>(bag.object, 0x40 + m.item * 16) == m.pouch &&
            at<int32_t>(bag.object, 0x44 + m.item * 16) > 0;
        if (owned) d.quantity_before = at<u32>(bag.object, 0x44 + m.item * 16);
        call<void>(base + 0x99e6b4, &bag);
        failure = ItemMissing;
        if (!owned || !call<bool>(base + 0xa5a070, inventory, m.item, 1U)) break;
        d.consumed = 1; d.stage = 5;
        if (!call<bool>(base + 0xbcea78, context.object))
            call<void>(base + 0xa5d060, m.item,
                       u32(call<uint16_t>(base + 0x3668, pokemon.object)),
                       u32(call<uint16_t>(base + 0x3678, pokemon.object)),
                       u32(call<uint8_t>(base + 0x975a0, pokemon.object)));
        if (healing) call<void>(base + 0x986638, uint8_t(10), 1U);
        if (call<bool>(base + 0x941b80, db, m.item)) call<void>(base + 0x986638, uint8_t(11), 1U);
        d.stage = 6;
        call<void>(base + 0xa5c988, &adapter, &pokemon, m.item);
        d.stage = 7; // void native return is NOT confirmation; inspect real state next tick
    } while (false);
    if (adapter.object) call<void>(base + 0x120500, adapter.object);
    releaseVirtual(binding.second, 0x18);
    releaseVirtual(binding.first, 0x18);
    call<void>(base + 0x556ca0, &mapped);
    call<void>(base + 0x52d53c, &status_target);
    call<void>(base + 0x52d49c, &status_service);
    releaseVirtual(actor.object, 0x60);
    call<void>(base + 0x696f10, &wrapper);
    call<void>(base + 0x556ca0, &pokemon);
    call<void>(base + 0x407900, &context);
    if (!d.consumed) finish(m, failure);
}
