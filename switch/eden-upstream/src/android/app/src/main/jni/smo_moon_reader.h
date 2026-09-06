#pragma once
#include "smo_state_reader.h"
#include <vector>

namespace SmoCompanion {
struct MoonEntry {
    std::uint32_t index{}, number{}, flags{}, reserved{};
    std::array<char, 64> stage{}, placement{};
};
static_assert(sizeof(MoonEntry) == 144);
struct MoonSnapshot {
    std::uint32_t magic{0x4d4f4d53}, version{1}, size{36896}, count{};
    std::uint64_t generation{};
    std::int32_t world{-1}, scenario{-1};
    std::array<MoonEntry, 256> entries{};
};
static_assert(sizeof(MoonSnapshot) == 36896);

// Cached indexing of the game's KNOWN HintInfo array, not a heap/signature scan.
// Poll this separately at collection-page cadence, never at rendering cadence.
class MoonReader {
    struct Index { int index{}, world{}; bool rock{}; MoonEntry names{}; bool has_names{}; };
    Address saved_owner{}, saved_table{};
    int saved_world{-1}, saved_registered_count{-1};
    std::vector<Index> indices;
public:
    void Reset() {
        saved_owner = saved_table = 0;
        saved_world = saved_registered_count = -1;
        indices.clear();
    }
    template <class Memory>
    bool Read(Memory& memory, Address base, const Identity& owner, const Snapshot& state,
              MoonSnapshot& output) {
        std::size_t budget{};
        auto bytes = [&](Address a, void* p, std::size_t n) {
            if (!a || a >= (Address{1} << 39) || n > (Address{1} << 39) - a ||
                n > 4096 || budget + n > 32768) return false;
            budget += n;
            return memory.Read(a, p, n);
        };
        auto ptr = [&](Address at) -> Address {
            Address a{};
            return bytes(at, &a, 8) && a && a % 8 == 0 && a < (Address{1} << 39) ? a : 0;
        };
        auto integer = [&](Address at) -> std::optional<std::int32_t> {
            std::int32_t value{};
            if (!bytes(at, &value, 4)) return std::nullopt;
            return value;
        };
        const auto table = ptr(owner.save + 0x9a0);
        const auto list = ptr(owner.holder + 0x190);
        const auto list_count = list ? integer(list) : std::nullopt;
        const auto array = list ? ptr(list + 8) : 0;
        const auto world_entry = array ? ptr(array + state.world * 8) : 0;
        const auto rock_scenario = world_entry ? integer(world_entry + 0x1c) : std::nullopt;
        const auto scenarios = ptr(owner.save + 0x7d8);
        const auto scenario = scenarios ? integer(scenarios + state.world * 4) : std::nullopt;
        // registerShineInfo (0x525a20) can populate previously empty HintInfo slots.
        // It increments this native per-world registration vector's count. Use that
        // change signal to invalidate metadata rather than rescanning every refresh.
        const auto registration_worlds = integer(owner.save + 0x9a8);
        const auto registrations = ptr(owner.save + 0x9b0);
        const auto registered = registrations ? ptr(registrations + state.world * 8) : 0;
        const auto registered_count = registered ? integer(registered) : std::nullopt;
        const auto registered_capacity = registered ? integer(registered + 4) : std::nullopt;
        if (!table || !list_count || state.world < 0 || state.world >= *list_count ||
            *list_count > 20 || !rock_scenario || !scenario || !registration_worlds ||
            *registration_worlds <= state.world || *registration_worlds > 20 ||
            !registered_count || !registered_capacity || *registered_count < 0 ||
            *registered_count > *registered_capacity || *registered_capacity > 1024) return false;
        if (saved_owner != owner.save || saved_table != table || saved_world != state.world ||
            saved_registered_count != *registered_count) {
            Reset();
            // Exactly the same 1024-element range as calcShineIndexTableNameAvailable.
            for (int i = 0; i < 1024; ++i) {
                std::array<std::uint8_t, 5> raw{};
                if (!bytes(table + i * 0x238 + 0x1cc, raw.data(), raw.size())) { Reset(); return false; }
                std::int32_t world{};
                std::memcpy(&world, raw.data(), 4);
                if (world < -1 || world >= *list_count || raw[4] > 1) { Reset(); return false; }
                if (world >= 0) indices.push_back(Index{i, world, raw[4] != 0, {}, false});
            }
            saved_owner = owner.save; saved_table = table;
            saved_world = state.world; saved_registered_count = *registered_count;
        }
        MoonSnapshot candidate;
        candidate.generation = state.generation;
        candidate.world = state.world;
        candidate.scenario = *scenario;
        for (auto& item : indices) {
            if (item.world != state.world || (item.rock && *scenario != *rock_scenario)) continue;
            if (candidate.count == candidate.entries.size()) return false;
            const auto address = table + item.index * 0x238;
            // Re-read visibility/collection, not names/metadata, on ordinary refreshes.
            std::array<std::uint8_t, 24> flags{};
            if (!bytes(address + 0x1cc, flags.data(), flags.size())) return false;
            std::int32_t world{}, name_state{}, hint{};
            std::memcpy(&world, flags.data(), 4);
            std::memcpy(&hint, flags.data() + 0x10, 4);
            std::memcpy(&name_state, flags.data() + 0x14, 4);
            if (world != item.world || (flags[4] != 0) != item.rock || flags[5] > 1) {
                Reset(); return false;
            }
            auto& entry = candidate.entries[candidate.count++];
            entry.index = item.index;
            entry.number = candidate.count;
            // ShineListLayout::updateWorldInfo (0x2041f4..0x204248) shows the
            // name when isGotShine succeeds, before testing isOpenShineName.
            // Name-unlock alone is insufficient for collected entries.
            const bool name_visible = flags[5] != 0 || name_state == 1;
            entry.flags = (flags[5] ? 1u : 0u) | (name_visible ? 2u : 0u) |
                          (item.rock ? 4u : 0u) | (hint != 0 ? 8u : 0u);
            if (name_visible) {
                if (!item.has_names) {
                    auto text = [&](Address at, std::array<char, 64>& out) {
                        Address a{}; // char strings need not be pointer-aligned.
                        return bytes(at, &a, 8) && bytes(a, out.data(), out.size()) &&
                               std::memchr(out.data(), 0, out.size());
                    };
                    if (!text(address + 8, item.names.stage) ||
                        !text(address + 0xa0, item.names.placement)) return false;
                    item.has_names = true;
                }
                entry.stage = item.names.stage;
                entry.placement = item.names.placement;
            } // Hidden entry names never cross the JNI boundary, even if cached earlier.
        }
        Snapshot after; Identity identity;
        if (!Reader(memory, base).Read(after, identity) || identity != owner ||
            after.world != state.world || ptr(owner.save + 0x9a0) != table ||
            integer(registered) != registered_count ||
            integer(scenarios + state.world * 4) != scenario) return false;
        output = candidate;
        return true;
    }
};
} // namespace SmoCompanion
