#include <cassert>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include "core/loader/xcx_actor_vitals.h"

int main() {
    using U64 = std::uint64_t;
    using U32 = std::uint32_t;
    constexpr U64 main = 0x10000000, object = 0x20000000;
    constexpr U64 component = 0x30000000, table = 0x11c32150;
    std::unordered_map<U64, U64> q{{object + 0x200, component}, {component, table},
        {table + 0xf8, main + 0x1cc92c}, {table + 0x110, main + 0x1cc978}};
    std::unordered_map<U64, U32> w{{component + 0x250, 321}, {component + 0x254, 999}};
    auto r64 = [&](U64 a) -> std::optional<U64> {
        auto i = q.find(a); return i == q.end() ? std::nullopt : std::optional(i->second);
    };
    auto r32 = [&](U64 a) -> std::optional<U32> {
        auto i = w.find(a); return i == w.end() ? std::nullopt : std::optional(i->second);
    };
    auto read = [&] { return XcxCompanionPayload::ReadActorVitals(r32, r64, main, object); };
    auto value = read();
    assert(value && value->hp == 321 && value->maximum_hp == 999);
    w[component + 0x250] = 0;
    assert(read() && read()->hp == 0); // A defeated character is valid, not unavailable.
    w[component + 0x250] = 1000;
    assert(!read());
    w[component + 0x250] = 1;
    q[table + 0xf8] += 4;
    assert(!read()); // A different getter is not the same object layout.
    q[table + 0xf8] -= 4;
    w.erase(component + 0x254);
    assert(!read());
    w[component + 0x254] = 999;
    int reads = 0;
    auto changed = [&](U64 a) -> std::optional<U64> {
        if (a == object + 0x200 && ++reads > 1) return component + 0x1000;
        return r64(a);
    };
    assert(!XcxCompanionPayload::ReadActorVitals(r32, changed, main, object));
    constexpr U64 stats = 0x31000000, data = 0x32000000;
    q[table + 0xc0] = main + 0x1d778c;
    q[table + 0x80] = main + 0x1cbbd4;
    q[component + 0x98] = stats;
    q[stats] = main + 0x1c49248;
    q[main + 0x1c49348] = main + 0x33e7c8;
    q[stats + 8] = data;
    std::uint8_t level = 42;
    auto r8 = [&](U64 address) -> std::optional<std::uint8_t> {
        if (address == data + 0x7a) return level;
        return std::nullopt;
    };
    auto read_level = [&] { return XcxCompanionPayload::ReadActorLevel(r8, r64, main, object); };
    assert(read_level() == 42);
    level = 43;
    assert(read_level() == 43); // Uses the current save-data value, not a cached level.
    level = 0;
    assert(!read_level());
    level = 42;
    q[stats] += 8;
    assert(!read_level());
    q[stats] -= 8;
    q[table + 0xc0] += 4;
    assert(!read_level());
    q[table + 0xc0] -= 4;
    reads = 0;
    assert(!XcxCompanionPayload::ReadActorLevel(r8, changed, main, object));
}
