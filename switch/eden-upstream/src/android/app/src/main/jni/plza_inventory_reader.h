#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Read-only v393216 bag lookup, traced from 2BCAC8 -> 2BC8EC/A5A268.
// The caller MUST gate on the full executable build ID before using these RVAs.
// This is the application's typed component vector, not a Satchel widget or the
// encrypted save-block mirror. Never retain an object pointer across samples.
namespace PlzaInventory {
using Address = std::uint64_t;
constexpr Address Ready = 0x610A980;
constexpr Address Components = 0x610A710;
constexpr std::size_t RecordCount = 3000;
constexpr std::size_t MaxComponents = 256;
struct Record {
    std::uint32_t pouch;
    std::int32_t quantity;
    std::uint32_t flags;
    std::uint32_t reserved;
    bool operator==(const Record&) const = default;
};
static_assert(sizeof(Record) == 0x10);
using Records = std::array<Record, RecordCount>;

template <typename Reader, typename T>
bool Read(Reader& reader, Address address, T& value) {
    return address >= 0x1000 && address <= UINT64_MAX - sizeof(T) &&
           reader(address, &value, sizeof(T));
}

// Exact native RTTI getters returning 3CFCB18. Checking executable bytes as well
// as the relocated pointer rejects unrelated/freed objects without a host-side
// virtual call or an unbounded RTTI walk.
template <typename Reader>
bool IsBagType(Reader& reader, Address base, Address object) {
    Address table{}, getter{};
    if ((object & 7) || !Read(reader, object + 0x28, table) || (table & 7) ||
        !Read(reader, table, getter) || getter < base) return false;
    const auto offset = getter - base;
    if (offset != 0x291AF34 && offset != 0x291B018 && offset != 0x291B024) return false;
    std::array<std::uint32_t, 3> code{};
    if (!Read(reader, getter, code)) return false;
    // ADRP x0,3CFC000; ADD x0,x0,B18; RET. Decode ADRP relative to
    // the loaded PC so a relocated application does not change the result.
    const auto adrp = code[0];
    if ((adrp & 0x9F00001F) != 0x90000000 || code[1] != 0x912C6000 ||
        code[2] != 0xD65F03C0) return false;
    std::int64_t immediate = ((adrp >> 29) & 3) | (((adrp >> 5) & 0x7FFFF) << 2);
    if (immediate & (1 << 20)) immediate -= 1 << 21;
    return (getter & ~Address{0xFFF}) + immediate * 4096 == base + 0x3CFC000;
}

struct Identity {
    Address control{}, token{}, interface_pointer{};
    std::uint32_t generation{};
    bool operator==(const Identity&) const = default;
};

template <typename Reader>
bool GetIdentity(Reader& reader, Address object, Identity& identity) {
    std::uint16_t strong{};
    return Read(reader, object + 0x38, identity.control) && !(identity.control & 7) &&
           Read(reader, identity.control, identity.token) && identity.token != 0 &&
           Read(reader, identity.control + 0x10, identity.interface_pointer) &&
           identity.interface_pointer == object + 0x28 &&
           Read(reader, identity.control + 0x1A, strong) && strong != 0 &&
           Read(reader, identity.control + 0x1C, identity.generation) &&
           identity.generation != UINT32_MAX;
}

// Only publishes a complete sample. Membership, type, reference generation,
// readiness and records must agree before/after the copy. This is a bounded
// non-owning observation, not permission to mutate or call through this object.
template <typename Reader>
bool ReadLiveBag(Reader& reader, Address base, Records& output, Address& owner) {
    owner = 0;
    std::uint32_t ready{};
    std::array<Address, 2> range{};
    if (!Read(reader, base + Ready, ready) || !ready ||
        !Read(reader, base + Components, range) || (range[0] & 7) ||
        range[1] <= range[0] || (range[1] - range[0]) % 8 ||
        (range[1] - range[0]) / 8 > MaxComponents) return false;
    for (Address slot = range[0]; slot < range[1]; slot += 8) {
        Address object{};
        Identity before{}, after{};
        if (!Read(reader, slot, object) || object < 0x1000 ||
            object > UINT64_MAX - 0x40 - sizeof(Records) ||
            !IsBagType(reader, base, object) || !GetIdentity(reader, object, before)) continue;
        Records candidate{}, verify{};
        if (!Read(reader, object + 0x40, candidate) ||
            !Read(reader, object + 0x40, verify) || candidate != verify) return false;
        for (const auto& record : candidate) {
            if ((record.pouch != UINT32_MAX && record.pouch > 7) || record.quantity < 0 ||
                record.quantity > 65535) return false;
        }
        std::array<Address, 2> final_range{};
        Address final_object{};
        if (!GetIdentity(reader, object, after) || before != after ||
            !IsBagType(reader, base, object) ||
            !Read(reader, base + Components, final_range) || range != final_range ||
            !Read(reader, slot, final_object) || object != final_object ||
            !Read(reader, base + Ready, ready) || !ready) return false;
        output = candidate;
        owner = object;
        return true;
    }
    return false;
}
} // namespace PlzaInventory
