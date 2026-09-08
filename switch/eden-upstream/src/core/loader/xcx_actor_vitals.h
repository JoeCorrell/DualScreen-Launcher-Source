// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

namespace XcxCompanionPayload {
// GetLv 0x3D5DD0 -> component +0xC0 (0x1D778C), +0x80 (0x1CBBD4),
// stats +0x100 (0x33E7C8). The final leaf reads stats->data(+8)->level(+0x7A).
// Constructor 0x33FDA0 supplies the exact stats table below.
template <typename Read8, typename Read64>
std::optional<std::uint8_t> ReadActorLevel(Read8 read8, Read64 read64,
                                         std::uint64_t main, std::uint64_t object) {
    if (!main || !object) return std::nullopt;
    const auto component = read64(object + 0x200);
    if (!component || !*component) return std::nullopt;
    const auto table = read64(*component);
    if (!table || !*table || read64(*table + 0xC0) != main + 0x1D778C ||
        read64(*table + 0x80) != main + 0x1CBBD4) return std::nullopt;
    const auto stats = read64(*component + 0x98);
    if (!stats || !*stats || read64(*stats) != main + 0x1C49248 ||
        read64(main + 0x1C49348) != main + 0x33E7C8) return std::nullopt;
    const auto data = read64(*stats + 8);
    if (!data || !*data) return std::nullopt;
    const auto level = read8(*data + 0x7A);
    if (!level || !*level || read64(*stats + 8) != data ||
        read64(*stats) != main + 0x1C49248 || read64(*component) != table ||
        read64(*component + 0x98) != stats || read64(object + 0x200) != component)
        return std::nullopt;
    return level;
}

struct ActorVitals {
    std::uint32_t hp;
    std::uint32_t maximum_hp;
};

// Exact 1.0.2: GetHP (0x3D5E50) calls component vtable+0xF8 with bank 2.
// 0x1CC92C reads bank*0xD8+0xA0; maximum getter 0x1CC978 reads +0xA4.
// The native refill path 0x1CC51C copies effective maximum +0x254 to HP +0x250.
// Only reproduce these proven leaf reads, never invoke a guest virtual from JNI.
template <typename Read32, typename Read64>
std::optional<ActorVitals> ReadActorVitals(Read32 read32, Read64 read64,
                                         std::uint64_t main, std::uint64_t object) {
    if (!main || !object) return std::nullopt;
    const auto component = read64(object + 0x200);
    if (!component || !*component) return std::nullopt;
    const auto table = read64(*component);
    if (!table || !*table || read64(*table + 0xF8) != main + 0x1CC92C ||
        read64(*table + 0x110) != main + 0x1CC978) return std::nullopt;
    const auto hp = read32(*component + 0x250);
    const auto maximum = read32(*component + 0x254);
    if (!hp || !maximum || !*maximum || *hp > *maximum ||
        *maximum > 100'000'000 || read64(object + 0x200) != component ||
        read64(*component) != table) return std::nullopt;
    return ActorVitals{*hp, *maximum};
}
} // namespace XcxCompanionPayload
